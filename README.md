# Transferência de funções para ITCM (STM32H755 CM7)

Este documento descreve, em ordem, os passos aplicados no projeto para executar funções críticas na ITCM (Instruction TCM), em vez de executar diretamente da FLASH.

## 1. Objetivo

- Reduzir latência de execução de funções matemáticas no CM7.
- Copiar código marcado para uma seção dedicada em ITCM durante o boot.
- Medir ganho de desempenho em ciclos de CPU.

## 2. Conceito usado

No linker, a seção de código da ITCM foi configurada com:

- VMA (endereço de execução): ITCMRAM
- LMA (endereço de carga): FLASH

Ou seja:

1. O binário dessas funções fica armazenado na FLASH.
2. No início da execução, o firmware copia esse bloco para a ITCM.
3. A CPU passa a executar essas funções a partir do endereço ITCM.

## 3. Ajuste do linker do CM7

Arquivo alterado:

- CM7/STM32H755ZITX_FLASH.ld

Antes de criar a seção `.itcm_text`, foi necessário garantir que a região ITCM existisse no bloco `MEMORY` do linker:

```ld
/* Memories definition */
MEMORY
{
   RAM_D1 (xrw)   : ORIGIN = 0x24000000, LENGTH =  512K
   FLASH   (rx)   : ORIGIN = 0x08000000, LENGTH = 1024K
   DTCMRAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
   RAM_D2 (xrw)   : ORIGIN = 0x30000000, LENGTH = 288K
   RAM_D3 (xrw)   : ORIGIN = 0x38000000, LENGTH = 64K
   ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
}
```

Sem essa entrada `ITCMRAM`, o linker não consegue posicionar `>ITCMRAM`, e o fluxo de copiar/executar da ITCM não funciona.

Foi adicionada uma seção dedicada para funções ITCM:

```ld
.itcm_text :
{
  . = ALIGN(4);
  _sitcm_ram = .;
  *(.itcm_text)
  *(.itcm_text*)
  . = ALIGN(4);
  _eitcm_ram = .;
} >ITCMRAM AT> FLASH

_sitcm_text = LOADADDR(.itcm_text);
```

Símbolos exportados:

- _sitcm_text: início na FLASH (origem da cópia)
- _sitcm_ram: início na ITCM (destino da cópia)
- _eitcm_ram: fim da região ITCM a copiar

## 4. Correção importante no layout da FLASH

A seção `.isr_vector` foi mantida no início da FLASH para não deslocar a tabela de vetores.

Isso evita falhas de exceção/trap na inicialização.

## 5. Rotina de copia FLASH -> ITCM

Arquivo:

- CM7/Core/Src/main.c

A rotina `Move_Code_To_ITCM()` copia palavra por palavra:

```c
uint32_t *pSrc = &_sitcm_text;
uint32_t *pDest = &_sitcm_ram;

while (pDest < &_eitcm_ram) {
  *pDest++ = *pSrc++;
}
__DSB();
__ISB();
```

- `__DSB()` e `__ISB()` garantem consistência antes de executar código recém-copiado.

## 6. Funcoes colocadas na ITCM

As funções foram marcadas com atributo de seção:

```c
__attribute__((section(".itcm_text")))
```

Funções de execução em ITCM:

- wrap_to_pi
- sin_itcm
- cos_itcm

## 7. Evolução da estratégia matemática

### 7.1. Primeira versão

- `fast_sin/fast_cos` em ITCM chamando `sinf/cosf` da libm.
- Resultado: parte do trabalho ainda ocorria na FLASH (libm).

### 7.2. Segunda versão

- Implementação polinomial própria em ITCM (`sin_itcm/cos_itcm`).
- Eliminou salto para `sinf/cosf` em cada chamada.

### 7.3. Versão atual (lookup table)

- LUT de seno com 1024 pontos (`sin_lut`).
- Interpolacao linear em `sin_itcm()`.
- `cos_itcm(x)` calculado por fase: `sin_itcm(HALF_PI_F + x)`.

Observação:

- A LUT é preenchida uma vez no boot em `SinLut_Init()`.
- O preenchimento usa `sinf` apenas na inicialização.
- Durante execução contínua, o caminho de cálculo usa LUT.

## 8. Benchmark por ciclos (DWT)

Foi implementado benchmark de comparação:

- `sin_itcm(x)` (ITCM)
- `sinf(x)` (FLASH/libm)

Funcoes:

- `DWT_CycleCounter_Init()`
- `Benchmark_Sin_Performance()`

Variáveis de resultado:

- `ciclos_sin_itcm_total`
- `ciclos_sinf_flash_total`
- `ciclos_sin_itcm_medio`
- `ciclos_sinf_flash_medio`

## 9. Validação no .map

Arquivo:

- CM7/Debug/h755_teste_CM7.map

Verificações realizadas:

1. Endereço de carga da seção ITCM em FLASH (`_sitcm_text`).
2. Endereço de execução da seção em ITCM (`_sitcm_ram`..`_eitcm_ram`).
3. Símbolos de funções em faixa ITCM (0x0000xxxx).

## 10. Resultado de desempenho observado

Exemplo medido:

- médio ITCM: 318 ciclos
- médio FLASH: 428 ciclos

Ganho percentual:

- (428 - 318) / 428 = 25.7%

Com CM7 a 480 MHz:

- ITCM: 318 / 480e6 = 0.6625 us
- FLASH: 428 / 480e6 = 0.8917 us

## 11. Erros encontrados e como foram corrigidos

1. Undefined reference para `_sitcm_text/_sitcm_ram/_eitcm_ram`:
   - causa: símbolos não definidos no linker CM7.
   - solução: criar seção `.itcm_text` e símbolos no linker script do CM7.

2. Trap/fault após alteração inicial do linker:
   - causa: possível deslocamento da `.isr_vector` do início da FLASH.
   - solução: manter `.isr_vector` no início e posicionar `.itcm_text` depois.

3. Section type conflict (`wrap_to_pi` x `sin_lut`):
   - causa: dados (LUT) colocados na mesma seção de código (`.itcm_text`).
   - solução: manter LUT como dado normal (`static float sin_lut[...]`) fora de `.itcm_text`.

## 12. Sequência recomendada para repetir em outro projeto

1. Definir secao `.itcm_text` no linker com `>ITCMRAM AT> FLASH`.
2. Exportar `_sitcm_text`, `_sitcm_ram`, `_eitcm_ram`.
3. Criar rotina de cópia no boot e chamar antes de usar funções ITCM.
4. Marcar funções-alvo com `__attribute__((section(".itcm_text")))`.
5. Confirmar no arquivo `.map` os endereços de carga/execução.
6. Medir desempenho com DWT para validar ganho real.

---