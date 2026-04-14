# Transferencia de funcoes para ITCM (STM32H755 CM7)

Este documento descreve, em ordem, os passos aplicados no projeto para executar funcoes criticas na ITCM (Instruction TCM) em vez de executar diretamente da FLASH.

## 1. Objetivo

- Reduzir latencia de execucao de funcoes matematicas no CM7.
- Copiar codigo marcado para uma secao dedicada em ITCM durante o boot.
- Medir ganho de desempenho em ciclos de CPU.

## 2. Conceito usado

No linker, a secao de codigo da ITCM foi configurada com:

- VMA (endereco de execucao): ITCMRAM
- LMA (endereco de carga): FLASH

Ou seja:

1. O binario dessas funcoes fica armazenado na FLASH.
2. No inicio da execucao, o firmware copia esse bloco para a ITCM.
3. A CPU passa a executar essas funcoes a partir do endereco ITCM.

## 3. Ajuste do linker do CM7

Arquivo alterado:

- CM7/STM32H755ZITX_FLASH.ld

Antes de criar a secao `.itcm_text`, foi necessario garantir que a regiao ITCM existisse no bloco `MEMORY` do linker:

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

Sem essa entrada `ITCMRAM`, o linker nao consegue posicionar `>ITCMRAM` e o fluxo de copia/executar da ITCM nao funciona.

Foi adicionada uma secao dedicada para funcoes ITCM:

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

Simbolos exportados:

- _sitcm_text: inicio na FLASH (origem da copia)
- _sitcm_ram: inicio na ITCM (destino da copia)
- _eitcm_ram: fim da regiao ITCM a copiar

## 4. Correcao importante no layout da FLASH

A secao `.isr_vector` foi mantida no inicio da FLASH para nao deslocar a tabela de vetores.

Isso evita falhas de excecao/trap na inicializacao.

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

- `__DSB()` e `__ISB()` garantem consistencia antes de executar codigo recem-copiado.

## 6. Funcoes colocadas na ITCM

As funcoes foram marcadas com atributo de secao:

```c
__attribute__((section(".itcm_text")))
```

Funcoes de execucao em ITCM:

- wrap_to_pi
- sin_itcm
- cos_itcm

## 7. Evolucao da estrategia matematica

### 7.1. Primeira versao

- `fast_sin/fast_cos` em ITCM chamando `sinf/cosf` da libm.
- Resultado: parte do trabalho ainda ocorria na FLASH (libm).

### 7.2. Segunda versao

- Implementacao polinomial propria em ITCM (`sin_itcm/cos_itcm`).
- Eliminou salto para `sinf/cosf` em cada chamada.

### 7.3. Versao atual (lookup table)

- LUT de seno com 1024 pontos (`sin_lut`).
- Interpolacao linear em `sin_itcm()`.
- `cos_itcm(x)` calculado por fase: `sin_itcm(HALF_PI_F + x)`.

Observacao:

- A LUT e preenchida uma vez no boot em `SinLut_Init()`.
- O preenchimento usa `sinf` apenas na inicializacao.
- Durante execucao continua, o caminho de calculo usa LUT.

## 8. Benchmark por ciclos (DWT)

Foi implementado benchmark de comparacao:

- `sin_itcm(x)` (ITCM)
- `sinf(x)` (FLASH/libm)

Funcoes:

- `DWT_CycleCounter_Init()`
- `Benchmark_Sin_Performance()`

Variaveis de resultado:

- `ciclos_sin_itcm_total`
- `ciclos_sinf_flash_total`
- `ciclos_sin_itcm_medio`
- `ciclos_sinf_flash_medio`

## 9. Validacao no .map

Arquivo:

- CM7/Debug/h755_teste_CM7.map

Verificacoes realizadas:

1. Endereco de carga da secao ITCM em FLASH (`_sitcm_text`).
2. Endereco de execucao da secao em ITCM (`_sitcm_ram`..`_eitcm_ram`).
3. Simbolos de funcoes em faixa ITCM (0x0000xxxx).

## 10. Resultado de desempenho observado

Exemplo medido:

- medio ITCM: 318 ciclos
- medio FLASH: 428 ciclos

Ganho percentual:

- (428 - 318) / 428 = 25.7%

Com CM7 a 480 MHz:

- ITCM: 318 / 480e6 = 0.6625 us
- FLASH: 428 / 480e6 = 0.8917 us

## 11. Erros encontrados e como foram corrigidos

1. Undefined reference para `_sitcm_text/_sitcm_ram/_eitcm_ram`:
   - causa: simbolos nao definidos no linker CM7.
   - solucao: criar secao `.itcm_text` e simbolos no linker script do CM7.

2. Trap/fault apos alteracao inicial do linker:
   - causa: possivel deslocamento da `.isr_vector` do inicio da FLASH.
   - solucao: manter `.isr_vector` no inicio e posicionar `.itcm_text` depois.

3. Section type conflict (`wrap_to_pi` x `sin_lut`):
   - causa: dados (LUT) colocados na mesma secao de codigo (`.itcm_text`).
   - solucao: manter LUT como dado normal (`static float sin_lut[...]`) fora de `.itcm_text`.

## 12. Sequencia recomendada para repetir em outro projeto

1. Definir secao `.itcm_text` no linker com `>ITCMRAM AT> FLASH`.
2. Exportar `_sitcm_text`, `_sitcm_ram`, `_eitcm_ram`.
3. Criar rotina de copia no boot e chamar antes de usar funcoes ITCM.
4. Marcar funcoes-alvo com `__attribute__((section(".itcm_text")))`.
5. Confirmar no arquivo `.map` os enderecos de carga/execucao.
6. Medir desempenho com DWT para validar ganho real.

---