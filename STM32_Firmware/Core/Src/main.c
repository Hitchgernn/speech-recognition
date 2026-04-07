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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

/* USER CODE BEGIN PV */
/* USER CODE BEGIN PV */
uint8_t current_mode = 1;

volatile uint8_t exti_flag = 0;
volatile uint32_t exti_timer = 0;

uint8_t led_pos = 0;
uint32_t mode1_timer = 0;

uint32_t sawtooth_val = 0;
uint8_t sawtooth_phase = 1;
uint32_t mode2_timer = 0;

uint32_t pot_val = 0;

uint8_t last_pb12 = 1, last_pb14 = 1;
uint32_t debounce_pb12 = 0, debounce_pb14 = 0;

uint8_t usb_received_data = 0;
uint8_t usb_data_ready = 0;
char ai_current_state = 'X';
uint32_t ai_timer = 0;
uint8_t ai_toggle = 0;
/* USER CODE END PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void Set_LEDs(uint8_t val) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, (val & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, (val & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, (val & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, (val & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, (val & 0x10) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, (val & 0x20) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, (val & 0x40) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, (val & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_13) {
        if (exti_flag == 0) {
            exti_flag = 1;
            exti_timer = HAL_GetTick();
            Set_LEDs(0xFF);
        }
    }
}

void Set_Green(uint8_t state) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Set_Blue(uint8_t state) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Matikan_Semua_Lampu(void) {
    Set_LEDs(0x00);
    Set_Green(0);
    Set_Blue(0);
}

void Handle_Interrupt_Task(void) {
    if (exti_flag == 1) {
        if (HAL_GetTick() - exti_timer >= 5000) {
            exti_flag = 0;
            Matikan_Semua_Lampu();
        }
    }
}

void Read_Buttons(void) {
    uint8_t pb12_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12);
    if (pb12_state == GPIO_PIN_RESET && last_pb12 == GPIO_PIN_SET && (HAL_GetTick() - debounce_pb12 > 50)) {
        if (current_mode == 0 || current_mode == 4) current_mode = 1;
        else {
            current_mode++;
            if (current_mode > 3) current_mode = 1;
        }
        Matikan_Semua_Lampu();
        debounce_pb12 = HAL_GetTick();
    }
    last_pb12 = pb12_state;

    uint8_t pb14_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14);
    if (pb14_state == GPIO_PIN_RESET && last_pb14 == GPIO_PIN_SET && (HAL_GetTick() - debounce_pb14 > 50)) {
        if (current_mode != 4) {
            current_mode = 4;
            Matikan_Semua_Lampu();
            ai_current_state = 'X';
            char msg[] = "\r\n[STM32] Mode AI Aktif!\r\n";
            CDC_Transmit_FS((uint8_t *)msg, strlen(msg));
        } else {
            current_mode = 0;
            Matikan_Semua_Lampu();
            char msg[] = "\r\n[STM32] Standby.\r\n";
            CDC_Transmit_FS((uint8_t *)msg, strlen(msg));
        }
        debounce_pb14 = HAL_GetTick();
    }
    last_pb14 = pb14_state;
}

void Run_Mode1_Shift(void) {
    if (HAL_GetTick() - mode1_timer >= 200) {
        mode1_timer = HAL_GetTick();
        Set_LEDs(1 << led_pos);
        led_pos++;
        if (led_pos > 7) led_pos = 0;
    }
}

void Run_Mode2_Sawtooth(void) {
    Set_LEDs(0x00);
    if (HAL_GetTick() - mode2_timer >= 50) {
        mode2_timer = HAL_GetTick();
        sawtooth_val++;

        // GANTI ANGKA 68 DAN 58 INI DENGAN NIM KELOMPOKMU!
        if (sawtooth_phase == 1) {
            if (sawtooth_val > 68) { sawtooth_val = 0; sawtooth_phase = 2; }
        } else {
            if (sawtooth_val > 58) { sawtooth_val = 0; sawtooth_phase = 1; }
        }
    }
}

void Run_Mode3_ADC(void) {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
        pot_val = HAL_ADC_GetValue(&hadc1);
        uint8_t num_leds = (pot_val * 9) / 4096;
        uint8_t led_pattern = (1 << num_leds) - 1;
        Set_LEDs(led_pattern);
    }
}

void Run_Mode4_AI(void) {
    if (usb_data_ready == 1) {
        usb_data_ready = 0;
        if (strchr("RSGHBVX", usb_received_data) != NULL) {
            ai_current_state = usb_received_data;
            Matikan_Semua_Lampu();
        }
    }

    if (ai_current_state == 'R') {
        if (HAL_GetTick() - ai_timer >= 200) {
            ai_timer = HAL_GetTick();
            ai_toggle = !ai_toggle;
            if (ai_toggle) Set_LEDs(0xFF); else Set_LEDs(0x00);
        }
    }
    else if (ai_current_state == 'S') { Set_LEDs(0xFF); }
    else if (ai_current_state == 'G') {
        if (HAL_GetTick() - ai_timer >= 200) {
            ai_timer = HAL_GetTick();
            ai_toggle = !ai_toggle;
            Set_Green(ai_toggle);
        }
    }
    else if (ai_current_state == 'H') { Set_Green(1); }
    else if (ai_current_state == 'B') {
        if (HAL_GetTick() - ai_timer >= 200) {
            ai_timer = HAL_GetTick();
            ai_toggle = !ai_toggle;
            Set_Blue(ai_toggle);
        }
    }
    else if (ai_current_state == 'V') { Set_Blue(1); }
    else { Matikan_Semua_Lampu(); }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	Handle_Interrupt_Task();

	if (exti_flag == 1) {
	   continue;
	}

	Read_Buttons();

	if (current_mode == 1) { Run_Mode1_Shift(); }
	else if (current_mode == 2) { Run_Mode2_Sawtooth(); }
	else if (current_mode == 3) { Run_Mode3_ADC(); }
	else if (current_mode == 4) { Run_Mode4_AI(); }
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA1 PA2 PA3
                           PA4 PA5 PA6 PA7
                           PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_MODE_Pin BTN_AI_Pin */
  GPIO_InitStruct.Pin = BTN_MODE_Pin|BTN_AI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_INTERRUPT_Pin */
  GPIO_InitStruct.Pin = BTN_INTERRUPT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_INTERRUPT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB4 PB5 PB6 PB7
                           PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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

#ifdef  USE_FULL_ASSERT
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

