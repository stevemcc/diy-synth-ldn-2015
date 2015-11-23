#include "ex06/main.h"
#include "ex06/stm32f4xx_it.h"

extern I2S_HandleTypeDef hAudioOutI2s;
extern I2S_HandleTypeDef hAudioInI2s;

__IO uint32_t millis = 0;

void Error_Handler(void) {
	BSP_LED_On(LED_RED);
	while (1) {
	}
}

void SysTick_Handler(void) {
	HAL_IncTick();
	millis++;
	if (millis >= 1000) {
		millis -= 1000;
	}
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (BSP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

void EXTI0_IRQHandler(void) {
	HAL_GPIO_EXTI_IRQHandler(KEY_BUTTON_PIN);
}

void EXTI4_IRQHandler(void) {
	HAL_GPIO_EXTI_IRQHandler(ACCELERO_INT1_PIN);
}

void I2S3_IRQHandler(void) {
	HAL_DMA_IRQHandler(hAudioOutI2s.hdmatx);
}

void I2S2_IRQHandler(void) {
	HAL_DMA_IRQHandler(hAudioInI2s.hdmarx);
}
