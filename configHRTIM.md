# Configuracao do HRTIM (commit 186031e)

Este documento resume o que foi configurado no ultimo commit para o HRTIM no core CM7, com frequencia de 40 kHz, pontos de comparacao em 10% e 90% do periodo, e dead time de 200 ns.

## 1) Arquivos alterados para habilitar HRTIM

- `CM7/Core/Inc/stm32h7xx_hal_conf.h`
  - Habilitou o modulo HAL do HRTIM:
  - `#define HAL_HRTIM_MODULE_ENABLED`
- `CM7/Core/Inc/hrtim.h`
  - Adicionado header do periferico e prototipo `MX_HRTIM_Init()`.
- `CM7/Core/Src/hrtim.c`
  - Adicionada toda a configuracao do HRTIM1 (Timer A, comparadores, dead time, saidas e GPIO).
- `CM7/Core/Src/main.c`
  - Incluiu `hrtim.h`, chamou `MX_HRTIM_Init()` e iniciou saidas/counters do HRTIM.

## 2) Calculo da frequencia e periodo

Objetivo: 40 kHz com clock de 200 MHz.

Formula:

- `Periodo = f_clk / f_pwm`
- `Periodo = 200.000.000 / 40.000 = 5.000`

Valor configurado em `hrtim.c`:

- `pTimeBaseCfg.Period = 0x1388` (decimal 5000)

## 3) Calculo dos comparadores (10% e 90%)

Com periodo total de 5000:

- Compare 1 (10%):
  - `5000 x 0,10 = 500`
  - Hexadecimal: `0x01F4`
- Compare 2 (90%):
  - `5000 x 0,90 = 4500`
  - Hexadecimal: `0x1194`

Valores aplicados em `hrtim.c`:

- `pCompareCfg.CompareValue = 0x1F4` para `HRTIM_COMPAREUNIT_1`
- `pCompareCfg.CompareValue = 0x1194` para `HRTIM_COMPAREUNIT_2`

## 4) Calculo do dead time (200 ns)

Clock do HRTIM: 200 MHz

- `T_tick = 1 / 200.000.000 = 5 ns`
- `Valor_deadtime = 200 ns / 5 ns = 40`

Hexadecimal de 40:

- `0x28`

Valores configurados em `hrtim.c`:

- `pDeadTimeCfg.RisingValue = 0x28`
- `pDeadTimeCfg.FallingValue = 0x28`
- Prescaler do dead time em DIV1.

## 5) Forma de onda configurada

No Timer A:

- Saida TA1:
  - `SetSource = HRTIM_OUTPUTSET_TIMCMP1`
  - `ResetSource = HRTIM_OUTPUTRESET_TIMCMP2`
- Saida TA2:
  - `SetSource = HRTIM_OUTPUTSET_NONE`
  - `ResetSource = HRTIM_OUTPUTRESET_NONE`
  - Escolha intencional: com `DeadTimeInsertion` habilitado no Timer A, o TA2 e utilizado como complementar do TA1 com insercao de dead time.

Interpretacao da TA1:

- Sobe no compare 1 (10% do periodo) e desce no compare 2 (90% do periodo).
- Isso gera pulso ativo de 80% do periodo, deslocado dentro do ciclo (de 10% ate 90%).

Interpretacao da TA2:

- Embora esteja com Set/Reset em NONE, ela foi iniciada para operar como saida complementar da TA1 quando a unidade de dead time do Timer A esta ativa.

## 6) GPIO e clock do periferico

Configuracoes realizadas no MSP (`HAL_HRTIM_MspInit` e `HAL_HRTIM_MspPostInit`):

- Clock do HRTIM1 habilitado.
- Clock de GPIOC habilitado.
- Pinos configurados:
  - PC6 -> HRTIM_CHA1
  - PC7 -> HRTIM_CHA2
  - Alternate Function: `GPIO_AF1_HRTIM1`

## 7) Como as funcoes foram inicializadas na main

No `CM7/Core/Src/main.c`, a sequencia relevante ficou:

1. `HAL_Init()`
2. `SystemClock_Config()`
3. `MX_GPIO_Init()`
4. `MX_HRTIM_Init()`
5. (USER CODE) `Move_Code_To_ITCM()`
6. (USER CODE) `SinLut_Init()`
7. (USER CODE) `DWT_CycleCounter_Init()`
8. (USER CODE) `Benchmark_Sin_Performance()`
9. `HAL_HRTIM_WaveformOutputStart(&hhrtim, HRTIM_OUTPUT_TA1 | HRTIM_OUTPUT_TA2)`
10. `HAL_HRTIM_WaveformCounterStart(&hhrtim, HRTIM_TIMERID_TIMER_A)`
11. `HAL_HRTIM_WaveformCounterStart(&hhrtim, HRTIM_TIMERID_MASTER)`

Ou seja: primeiro inicializa o periferico (`MX_HRTIM_Init`), depois habilita as saídas e inicia os contadores para a geracao da forma de onda.

## 8) Referencias diretas no codigo

- `CM7/Core/Inc/stm32h7xx_hal_conf.h`: habilitacao do modulo HRTIM.
- `CM7/Core/Src/hrtim.c`: periodo, comparadores, dead time e configuracao de saidas.
- `CM7/Core/Src/main.c`: chamada de init e start do HRTIM.

## 9) Como alterar a forma do PWM (passo a passo)

Se quiser alterar a forma do PWM em runtime, aplique o compare novamente seguindo este fluxo:

1. Crie e zere a estrutura `HRTIM_CompareCfgTypeDef`.
2. Defina o novo `CompareValue`.
3. Chame `HAL_HRTIM_WaveformCompareConfig(...)` para a unidade desejada.
4. Em caso de erro, chame `Error_Handler()`.

Exemplo (alterando o Compare Unit 1 para `0x3E8`):

```c
HRTIM_CompareCfgTypeDef pCompareCfg = {0};
pCompareCfg.CompareValue = 0x3E8;
if (HAL_HRTIM_WaveformCompareConfig(&hhrtim, HRTIM_TIMERINDEX_TIMER_A, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
{
  Error_Handler();
}
```

Observacao:

- Para manter o formato de bordas atual (sobe em CMP1 e desce em CMP2), ajuste CMP1 e CMP2 de forma coordenada.
