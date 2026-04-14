/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <math.h>
#include <stdint.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* DUAL_CORE_BOOT_SYNC_SEQUENCE: Define for dual core boot synchronization    */
/*                             demonstration code based on hardware semaphore */
/* This define is present in both CM7/CM4 projects                            */
/* To comment when developping/debugging on a single core                     */
#define DUAL_CORE_BOOT_SYNC_SEQUENCE

#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

#define PI_F 3.14159265359f
#define TWO_PI_F 6.28318530718f
#define HALF_PI_F 1.57079632679f
#define SIN_LUT_SIZE 1024U
#define SIN_LUT_INV_STEP ((float)SIN_LUT_SIZE / TWO_PI_F)

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* Símbolos exportados pelo Linker Script (.ld) */
extern uint32_t _sitcm_text; // Endereço de carga (LMA) na FLASH
extern uint32_t _sitcm_ram;  // Endereço de execução (VMA) no início da ITCM RAM
extern uint32_t _eitcm_ram;  // Endereço de fim da seção na ITCM RAM

float resultado_sin[2], resultado_cos[2];
volatile uint32_t ciclos_sin_itcm_total = 0;
volatile uint32_t ciclos_sinf_flash_total = 0;
volatile uint32_t ciclos_sin_itcm_medio = 0;
volatile uint32_t ciclos_sinf_flash_medio = 0;
volatile float benchmark_sink = 0.0f;

static float sin_lut[SIN_LUT_SIZE + 1U];
static uint8_t sin_lut_ready = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static void DWT_CycleCounter_Init(void);
static void Benchmark_Sin_Performance(void);
static void SinLut_Init(void);

void Move_Code_To_ITCM(void)
{
  uint32_t *pSrc = &_sitcm_text;
  uint32_t *pDest = &_sitcm_ram;

  // Copia palavra por palavra (32 bits) conforme a granularidade suportada [3]
  while (pDest < &_eitcm_ram)
  {
    *pDest++ = *pSrc++;
  }

  // Barreira de memória para garantir que o código esteja pronto para execução
  __DSB();
  __ISB();
}

__attribute__((section(".itcm_text"))) static float wrap_to_pi(float x)
{
  while (x > PI_F)
  {
    x -= TWO_PI_F;
  }
  while (x < -PI_F)
  {
    x += TWO_PI_F;
  }
  return x;
}

static void SinLut_Init(void)
{
  uint32_t i;

  if (sin_lut_ready != 0U)
  {
    return;
  }

  for (i = 0; i <= SIN_LUT_SIZE; i++)
  {
    float angle = ((float)i * TWO_PI_F) / (float)SIN_LUT_SIZE;
    sin_lut[i] = sinf(angle);
  }

  sin_lut_ready = 1U;
}

/* Implementacao em ITCM com lookup table + interpolacao linear */
__attribute__((section(".itcm_text"))) float sin_itcm(float x)
{
  float idx_f;
  uint32_t idx;
  float frac;
  float y0;
  float y1;

  x = wrap_to_pi(x);

  if (x < 0.0f)
  {
    x += TWO_PI_F;
  }

  idx_f = x * SIN_LUT_INV_STEP;
  idx = (uint32_t)idx_f;

  if (idx >= SIN_LUT_SIZE)
  {
    idx = SIN_LUT_SIZE - 1U;
    frac = 1.0f;
  }
  else
  {
    frac = idx_f - (float)idx;
  }

  y0 = sin_lut[idx];
  y1 = sin_lut[idx + 1U];

  return y0 + (y1 - y0) * frac;
}

__attribute__((section(".itcm_text"))) float cos_itcm(float x)
{
  return sin_itcm(HALF_PI_F + x);
}

static void DWT_CycleCounter_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void Benchmark_Sin_Performance(void)
{
  uint32_t ini;
  uint32_t fim;
  uint32_t i;
  float x = 0.1f;
  const uint32_t iteracoes = 2000U;
  volatile float acc = 0.0f;

  ini = DWT->CYCCNT;
  for (i = 0; i < iteracoes; i++)
  {
    acc += sin_itcm(x);
    x += 0.001f;
  }
  fim = DWT->CYCCNT;
  ciclos_sin_itcm_total = fim - ini;

  x = 0.1f;
  ini = DWT->CYCCNT;
  for (i = 0; i < iteracoes; i++)
  {
    acc += sinf(x);
    x += 0.001f;
  }
  fim = DWT->CYCCNT;
  ciclos_sinf_flash_total = fim - ini;

  ciclos_sin_itcm_medio = ciclos_sin_itcm_total / iteracoes;
  ciclos_sinf_flash_medio = ciclos_sinf_flash_total / iteracoes;
  benchmark_sink = acc;
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  int32_t timeout;
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_0 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* Wait until CPU2 boots and enters in stop mode or timeout*/
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0))
    ;
  if (timeout < 0)
  {
    Error_Handler();
  }
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* When system initialization is finished, Cortex-M7 will release Cortex-M4 by means of
  HSEM notification */
  /*HW semaphore Clock enable*/
  __HAL_RCC_HSEM_CLK_ENABLE();
  /*Take HSEM */
  HAL_HSEM_FastTake(HSEM_ID_0);
  /*Release HSEM in order to notify the CPU2(CM4)*/
  HAL_HSEM_Release(HSEM_ID_0, 0);
  /* wait until CPU2 wakes up from stop mode */
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0))
    ;
  if (timeout < 0)
  {
    Error_Handler();
  }
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */

  Move_Code_To_ITCM();
  SinLut_Init();
  DWT_CycleCounter_Init();
  Benchmark_Sin_Performance();

  float angulo = 0.5f;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    resultado_sin[0] = sin_itcm(angulo);
    resultado_sin[1] = sinf(angulo);
    resultado_cos[0] = cos_itcm(angulo);
    resultado_cos[1] = cosf(angulo);

    angulo += 0.01f;
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_SMPS_2V5_SUPPLIES_LDO);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
