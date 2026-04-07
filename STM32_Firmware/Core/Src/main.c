/* user code begin header */
/**
 ******************************************************************************
	* @file           : main.c
	* @brief          : main program body
	******************************************************************************
	* @attention
	*
	* copyright (c) 2026 stmicroelectronics.
	* all rights reserved.
	*
	* this software is licensed under terms that can be found in the license file
	* in the root directory of this software component.
	* if no license file comes with this software, it is provided as-is.
	*
	******************************************************************************
	*/
/* user code end header */
/* includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* private includes ----------------------------------------------------------*/
/* user code begin includes */
#include "usbd_cdc_if.h" // wajib untuk fungsi kirim usb
#include "string.h"      // wajib untuk strlen()
/* user code end includes */

/* private typedef -----------------------------------------------------------*/
/* user code begin ptd */

/* user code end ptd */

/* private define ------------------------------------------------------------*/
/* user code begin pd */

/* user code end pd */

/* private macro -------------------------------------------------------------*/
/* user code begin pm */

/* user code end pm */

/* private variables ---------------------------------------------------------*/
adc_handletypedef hadc1;

/* user code begin pv */
// --- variabel state machine ---
uint8_t current_mode = 1; // 1=shift, 2=sawtooth, 3=ai mode

// --- variabel interupsi (exti pb13) ---
volatile uint8_t exti_flag = 0;
volatile uint32_t exti_timer = 0;

// --- variabel mode 1 (shift) ---
uint8_t led_pos = 0;
uint32_t mode1_timer = 0;

// --- variabel mode 2 (sawtooth) ---
uint32_t sawtooth_val = 0;
uint8_t sawtooth_phase = 1;
uint32_t mode2_timer = 0;

// --- variabel mode 3 (potensiometer) ---
uint32_t pot_val = 0; // menyimpan nilai adc (0-4095) untuk cubemonitor

// --- variabel debounce tombol ---
uint8_t last_pb12 = 1, last_pb14 = 1;
uint32_t debounce_pb12 = 0, debounce_pb14 = 0;

// --- variabel jembatan usb ai ---
uint8_t usb_received_data = 0; // menyimpan huruf dari python
uint8_t usb_data_ready = 0;    // flag pesan masuk
uint8_t ai_led_pattern = 0x00; // pola led yang dikendalikan ai

// --- variabel animasi ai mode ---
char ai_current_state = 'x'; // menyimpan status ai (a, c, atau x), default: x (mati)
uint32_t ai_timer = 0;       // timer untuk kedipan ai
uint8_t ai_toggle = 0;       // penanda nyala/mati bergantian
/* user code end pv */

/* private function prototypes -----------------------------------------------*/
void systemclock_config(void);
static void mx_gpio_init(void);
static void mx_adc1_init(void);
/* user code begin pfp */

/* user code end pfp */

/* private user code ---------------------------------------------------------*/
/* user code begin 0 */
/* USER CODE BEGIN 0 */

void Set_LEDs(uint8_t val)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, (val & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, (val & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, (val & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, (val & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, (val & 0x10) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, (val & 0x20) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, (val & 0x40) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, (val & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)
    { // jika pb13 (interrupt) ditekan
        if (exti_flag == 0)
        {
            exti_flag = 1;
            exti_timer = HAL_GetTick();
            Set_LEDs(0xFF);
        }
    }
}

void Set_Green(uint8_t state)
{
    // hijau di pb7, pb8, pb9
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Set_Blue(uint8_t state)
{
    // biru di pb4, pb5, pb6
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Matikan_Semua_Lampu(void)
{
    Set_LEDs(0x00); // matikan 8 merah (pa0-pa7)
    Set_Green(0);   // matikan 3 hijau (pb7-pb9)
    Set_Blue(0);    // matikan 3 biru  (pb4-pb6)
}

// ... (Biarkan fungsi Handle_Interrupt_Task, Read_Buttons, Run_Mode1, dsb yang di bawahnya TETAP ADA) ...
// --- 1. FUNGSI INTERUPSI (EXTI 5 Detik) ---
void Handle_Interrupt_Task(void) {
    if (exti_flag == 1) {
        if (HAL_GetTick() - exti_timer >= 5000) {
            exti_flag = 0;
            Matikan_Semua_Lampu();
        }
    }
}

// --- 2. FUNGSI PEMBACAAN TOMBOL ---
void Read_Buttons(void) {
    // PB12: Cycle Mode 1 -> Mode 2 -> Mode 3
    uint8_t pb12_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12);
    if (pb12_state == GPIO_PIN_RESET && last_pb12 == GPIO_PIN_SET && (HAL_GetTick() - debounce_pb12 > 50)) {
        if (current_mode == 0 || current_mode == 4) {
            current_mode = 1;
        } else {
            current_mode++; 
            if (current_mode > 3) current_mode = 1; 
        }
        Matikan_Semua_Lampu();
        debounce_pb12 = HAL_GetTick();
    }
    last_pb12 = pb12_state;

    // PB14: Masuk / Keluar Mode AI (Mode 4)
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

// --- 3. FUNGSI MODE 1 (SHIFT LEFT) ---
void Run_Mode1_Shift(void) {
    if (HAL_GetTick() - mode1_timer >= 200) {
        mode1_timer = HAL_GetTick();
        Set_LEDs(1 << led_pos);
        led_pos++;
        if (led_pos > 7) led_pos = 0;
    }
}

// --- 4. FUNGSI MODE 2 (SAWTOOTH NIM) ---
void Run_Mode2_Sawtooth(void) {
    Set_LEDs(0x00);
    if (HAL_GetTick() - mode2_timer >= 50) {
        mode2_timer = HAL_GetTick();
        sawtooth_val++;

        // JANGAN LUPA: Ganti angka 68 dan 58 di bawah ini dengan 2 Digit NIM Kelompokmu!
        if (sawtooth_phase == 1) {
            if (sawtooth_val > 68) { sawtooth_val = 0; sawtooth_phase = 2; }
        } else {
            if (sawtooth_val > 58) { sawtooth_val = 0; sawtooth_phase = 1; }
        }
    }
}

// --- 5. FUNGSI MODE 3 (ADC POTENSIO) ---
void Run_Mode3_ADC(void) {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
        pot_val = HAL_ADC_GetValue(&hadc1);
        uint8_t num_leds = (pot_val * 9) / 4096;
        uint8_t led_pattern = (1 << num_leds) - 1;
        Set_LEDs(led_pattern);
    }
}

// --- 6. FUNGSI MODE 4 (AI LISTENER) ---
void Run_Mode4_AI(void) {
    if (usb_data_ready == 1) {
        usb_data_ready = 0;
        // Ganti string di bawah ini menjadi HURUF KAPITAL semua
        if (strchr("RSGHBVX", usb_received_data) != NULL) {
            ai_current_state = usb_received_data;
            Matikan_Semua_Lampu();
        }
    }

    // Ganti semua pengecekan karakter di bawah ini menjadi KAPITAL
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
/* user code end 0 */

/**
 * @brief  the application entry point.
 * @retval int
 */
int main(void)
{

	/* user code begin 1 */

	/* user code end 1 */

	/* mcu configuration--------------------------------------------------------*/

	/* reset of all peripherals, initializes the flash interface and the systick. */
	hal_init();

	/* user code begin init */

	/* user code end init */

	/* configure the system clock */
	systemclock_config();

	/* user code begin sysinit */

	/* user code end sysinit */

	/* initialize all configured peripherals */
	mx_gpio_init();
	mx_usb_device_init();
	mx_adc1_init();
	/* user code begin 2 */

	/* user code end 2 */

	/* infinite loop */
	/* user code begin while */
	while (1)
	{
		// 1. Cek apakah ada interupsi 5 detik yang sedang berjalan
        Handle_Interrupt_Task();
        
        // Jika sedang interupsi, pause (lewati) pembacaan mode di bawahnya
        if (exti_flag == 1) {
            continue; 
        }

        // 2. Baca input tombol
        Read_Buttons();

        // 3. Jalankan Mode yang sedang aktif
        if (current_mode == 1) {
            Run_Mode1_Shift();
        } 
        else if (current_mode == 2) {
            Run_Mode2_Sawtooth();
        } 
        else if (current_mode == 3) {
            Run_Mode3_ADC();
        } 
        else if (current_mode == 4) {
            Run_Mode4_AI();
        }
	}
	/* user code end while */

	/* user code begin 3 */
	/* user code end 3 */
}

/**
 * @brief system clock configuration
 * @retval none
 */
void systemclock_config(void)
{
	rcc_oscinittypedef rcc_oscinitstruct = {0};
	rcc_clkinittypedef rcc_clkinitstruct = {0};

	/** configure the main internal regulator output voltage
	 */
	__hal_rcc_pwr_clk_enable();
	__hal_pwr_voltagescaling_config(pwr_regulator_voltage_scale2);

	/** initializes the rcc oscillators according to the specified parameters
	 * in the rcc_oscinittypedef structure.
	 */
	rcc_oscinitstruct.oscillatortype = rcc_oscillatortype_hse;
	rcc_oscinitstruct.hsestate = rcc_hse_on;
	rcc_oscinitstruct.pll.pllstate = rcc_pll_on;
	rcc_oscinitstruct.pll.pllsource = rcc_pllsource_hse;
	rcc_oscinitstruct.pll.pllm = 25;
	rcc_oscinitstruct.pll.plln = 336;
	rcc_oscinitstruct.pll.pllp = rcc_pllp_div4;
	rcc_oscinitstruct.pll.pllq = 7;
	if (hal_rcc_oscconfig(&rcc_oscinitstruct) != hal_ok)
	{
		error_handler();
	}

	/** initializes the cpu, ahb and apb buses clocks
	 */
	rcc_clkinitstruct.clocktype = rcc_clocktype_hclk | rcc_clocktype_sysclk | rcc_clocktype_pclk1 | rcc_clocktype_pclk2;
	rcc_clkinitstruct.sysclksource = rcc_sysclksource_pllclk;
	rcc_clkinitstruct.ahbclkdivider = rcc_sysclk_div1;
	rcc_clkinitstruct.apb1clkdivider = rcc_hclk_div2;
	rcc_clkinitstruct.apb2clkdivider = rcc_hclk_div1;

	if (hal_rcc_clockconfig(&rcc_clkinitstruct, flash_latency_2) != hal_ok)
	{
		error_handler();
	}
}

/**
 * @brief adc1 initialization function
 * @param none
 * @retval none
 */
static void mx_adc1_init(void)
{

	/* user code begin adc1_init 0 */

	/* user code end adc1_init 0 */

	adc_channelconftypedef sconfig = {0};

	/* user code begin adc1_init 1 */

	/* user code end adc1_init 1 */

	/** configure the global features of the adc (clock, resolution, data alignment and number of conversion)
	 */
	hadc1.instance = adc1;
	hadc1.init.clockprescaler = adc_clock_sync_pclk_div4;
	hadc1.init.resolution = adc_resolution_12b;
	hadc1.init.scanconvmode = disable;
	hadc1.init.continuousconvmode = disable;
	hadc1.init.discontinuousconvmode = disable;
	hadc1.init.externaltrigconvedge = adc_externaltrigconvedge_none;
	hadc1.init.externaltrigconv = adc_software_start;
	hadc1.init.dataalign = adc_dataalign_right;
	hadc1.init.nbrofconversion = 1;
	hadc1.init.dmacontinuousrequests = disable;
	hadc1.init.eocselection = adc_eoc_single_conv;
	if (hal_adc_init(&hadc1) != hal_ok)
	{
		error_handler();
	}

	/** configure for the selected adc regular channel its corresponding rank in the sequencer and its sample time.
	 */
	sconfig.channel = adc_channel_8;
	sconfig.rank = 1;
	sconfig.samplingtime = adc_sampletime_3cycles;
	if (hal_adc_configchannel(&hadc1, &sconfig) != hal_ok)
	{
		error_handler();
	}
	/* user code begin adc1_init 2 */

	/* user code end adc1_init 2 */
}

/**
 * @brief gpio initialization function
 * @param none
 * @retval none
 */
static void mx_gpio_init(void)
{
	gpio_inittypedef gpio_initstruct = {0};
	/* user code begin mx_gpio_init_1 */

	/* user code end mx_gpio_init_1 */

	/* gpio ports clock enable */
	__hal_rcc_gpioh_clk_enable();
	__hal_rcc_gpioa_clk_enable();
	__hal_rcc_gpiob_clk_enable();

	/*configure gpio pin output level */
	hal_gpio_writepin(gpioa, gpio_pin_0 | gpio_pin_1 | gpio_pin_2 | gpio_pin_3 | gpio_pin_4 | gpio_pin_5 | gpio_pin_6 | gpio_pin_7 | gpio_pin_8, gpio_pin_reset);

	/*configure gpio pin output level */
	hal_gpio_writepin(gpiob, gpio_pin_4 | gpio_pin_5 | gpio_pin_6 | gpio_pin_7 | gpio_pin_8 | gpio_pin_9, gpio_pin_reset);

	/*configure gpio pins : pa0 pa1 pa2 pa3
								pa4 pa5 pa6 pa7
								pa8 */
	gpio_initstruct.pin = gpio_pin_0 | gpio_pin_1 | gpio_pin_2 | gpio_pin_3 | gpio_pin_4 | gpio_pin_5 | gpio_pin_6 | gpio_pin_7 | gpio_pin_8;
	gpio_initstruct.mode = gpio_mode_output_pp;
	gpio_initstruct.pull = gpio_nopull;
	gpio_initstruct.speed = gpio_speed_freq_low;
	hal_gpio_init(gpioa, &gpio_initstruct);

	/*configure gpio pins : btn_mode_pin btn_ai_pin */
	gpio_initstruct.pin = btn_mode_pin | btn_ai_pin;
	gpio_initstruct.mode = gpio_mode_input;
	gpio_initstruct.pull = gpio_pullup;
	hal_gpio_init(gpiob, &gpio_initstruct);

	/*configure gpio pin : btn_interrupt_pin */
	gpio_initstruct.pin = btn_interrupt_pin;
	gpio_initstruct.mode = gpio_mode_it_falling;
	gpio_initstruct.pull = gpio_pullup;
	hal_gpio_init(btn_interrupt_gpio_port, &gpio_initstruct);

	/*configure gpio pins : pb4 pb5 pb6 pb7
								pb8 pb9 */
	gpio_initstruct.pin = gpio_pin_4 | gpio_pin_5 | gpio_pin_6 | gpio_pin_7 | gpio_pin_8 | gpio_pin_9;
	gpio_initstruct.mode = gpio_mode_output_pp;
	gpio_initstruct.pull = gpio_nopull;
	gpio_initstruct.speed = gpio_speed_freq_low;
	hal_gpio_init(gpiob, &gpio_initstruct);

	/* exti interrupt init*/
	hal_nvic_setpriority(exti15_10_irqn, 0, 0);
	hal_nvic_enableirq(exti15_10_irqn);

	/* user code begin mx_gpio_init_2 */

	/* user code end mx_gpio_init_2 */
}

/* user code begin 4 */

/* user code end 4 */

/**
 * @brief  this function is executed in case of error occurrence.
 * @retval none
 */
void error_handler(void)
{
	/* user code begin error_handler_debug */
	/* user can add his own implementation to report the hal error return state */
	__disable_irq();
	while (1)
	{
	}
	/* user code end error_handler_debug */
}

#ifdef use_full_assert
/**
 * @brief  reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval none
 */
void assert_failed(uint8_t *file, uint32_t line)
{
	/* user code begin 6 */
	/* user can add his own implementation to report the file name and line number,
		ex: printf("wrong parameters value: file %s on line %d\r\n", file, line) */
	/* user code end 6 */
}
#endif /* use_full_assert */

