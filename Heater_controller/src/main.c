#include "main.h"

#include "flash.h"
#include "usart.h"
#include "RTC.h"

// Массив для отправки в USART
char buf[100];
// Массивы для хранения параметров точек
u32 setting_time[30] = {0};
u32 setting_temp[30] = {0};
// Переменная хранения количества точек
u8 cycles = 0;
// Переменная хранения времени
u8 timer = 0;

// Счетчик принятых байт по USART
u8 receivedDataCounter = 0;
// Будем принимать
u8 receivedData[16];
u8 bytesToReceive =16;

int main(void) {

	RCC_init();
	USART_init();
	ADC1_init();
	// RTC_Init();
	GPIO_init();

	/* Чтение параметров из flash */
	// Читаем количество точек
	cycles = FLASH_Read(page31);
	// Читаем параметры точек
	for(u8 i = 0; i < cycles; i++) {

		setting_time[i] = FLASH_Read(i*4 + (page31) + 4);
		setting_temp[i] = FLASH_Read(i*4 + (page31) + 4 + cycles * 4);
	}

	// Печатаем параметры точек
	USART1_Send_String("Params in flash:\r\n");
	for(u8 i = 0; i < cycles; i++) {

		sprintf(buf, "Temp %d: %ld C, Time %d: %ld min\r\n", i,
				FLASH_Read(i*4 + (page31) + 4), i,
				FLASH_Read(i*4 + (page31) + 4 + cycles * 4));
		USART1_Send_String(buf);
	}

	// Запускаем работу
	StartWork();
}

// Обработчик прерываний от USART1
void USART1_IRQHandler(void) {

	// Выясняем, какое именно событие вызвало прерывание. Если это приём байта в RxD - обрабатываем.
	if (USART1->SR & USART_SR_RXNE) {

		// Сбрасываем флаг прерывания
		USART1->SR &=~USART_SR_RXNE;

		// Принимаем данные и пишем в буфер
		receivedData[receivedDataCounter]  = USART1->DR;

		// Приняли, увеличиваем значение счетчика
		receivedDataCounter ++;
	}
	// Записываем новые параметры точек во флеш
	// WriteNewParams();
}

// Обработчик прерываний от RTC
void RTC_IRQHandler(void) {

	// Прерывание по прошествии секунды
	if(RTC->CRL & RTC_CRL_SECF) {

		// Сбрасываем флаг
		RTC->CRL &= ~RTC_CRL_SECF;

		// Увеличиваем переменную времени
		timer++;

		/*
		if (timer == 60) {

			USART1_Send_String("\r\n1");
			timer = 0;
		}
		 */
	}
}

void ADC1_init(void) {

	// Set up RCC for ADC1
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
	// Set up ADC on PA0 Pin:  CNF0, CNF1, MODE0, MODE1 == 0;
	GPIOA->CRL &= ~(GPIO_CRL_MODE0_0 | GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_0 | GPIO_CRL_CNF0_1);

	ADC1->SMPR2 |= ADC_SMPR2_SMP0;
	// Set up Continous mode ADC
	ADC1->CR2 |= ADC_CR2_CONT | ADC_CR2_ADON | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL | ADC_CR2_JEXTSEL;
	ADC1->CR2 |= ADC_CR2_SWSTART;
}

void GPIO_init(void) {

	// Clear PC13 bit (ONBOARD RED LED)
	GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
	// Configure PC13 as Push Pull output at max 10Mhz
	GPIOC->CRH |= GPIO_CRH_MODE13_0;
}

void RCC_init(void) {

	uint32_t StartUpCounter = 0, HSEStatus = 0;

	/* Конфигурация  SYSCLK, HCLK, PCLK2 и PCLK1 */
	/* Включаем HSE */
	RCC->CR |= ((uint32_t)RCC_CR_HSEON);

	/* Ждем пока HSE не выставит бит готовности либо не выйдет таймаут*/
	do {

		HSEStatus = RCC->CR & RCC_CR_HSERDY;
		StartUpCounter++;
	}
	while((HSEStatus == 0) && (StartUpCounter != HSEStartUp_TimeOut));

	if ((RCC->CR & RCC_CR_HSERDY) != RESET) {

		HSEStatus = (uint32_t)0x01;
	} else {

		HSEStatus = (uint32_t)0x00;
	}

	/* Если HSE запустился нормально */
	if (HSEStatus == (uint32_t)0x01)
	{
		/* Включаем буфер предвыборки FLASH */
		FLASH->ACR |= FLASH_ACR_PRFTBE;

		/* Конфигурируем Flash на 2 цикла ожидания 												*/
		/* Это нужно потому, что Flash не может работать на высокой частоте 					*/
		/* если это не сделать, то будет странный глюк. Проц может запуститься, но через пару 	*/
		/* секунд повисает без "видимых причин". Вот такие вот неочевидные вилы. 				*/
		FLASH->ACR &= (uint32_t)((uint32_t)~FLASH_ACR_LATENCY);
		FLASH->ACR |= (uint32_t)FLASH_ACR_LATENCY_2;

		/* HCLK = SYSCLK 															*/
		/* AHB Prescaler = 1 														*/
		RCC->CFGR |= (uint32_t)RCC_CFGR_HPRE_DIV1;

		/* PCLK2 = HCLK 															*/
		/* APB2 Prescaler = 1 														*/
		RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE2_DIV1;

		/* PCLK1 = HCLK 															*/
		/* APB1 Prescaler = 2 														*/
		RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE1_DIV2;

		/* Конфигурируем множитель PLL configuration: PLLCLK = HSE * 9 = 72 MHz 	*/
		/* При условии, что кварц на 8МГц! 										*/
		/* RCC_CFGR_PLLMULL9 - множитель на 9. Если нужна другая частота, не 72МГц 	*/
		/* то выбираем другой множитель. 											*/

		/* Сбрасываем в нули прежнее значение*/
		RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL));

		/* А теперь накатываем новое 												*/
		/* RCC_CFGR_PLLSRC_HSE -- выбираем HSE на вход 								*/
		/* RCC_CFGR_PLLMULL9 -- множитель 9											*/
		RCC->CFGR |= (uint32_t)(RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL9);

		/* Все настроили? Включаем PLL */
		RCC->CR |= RCC_CR_PLLON;

		/* Ожидаем, пока PLL выставит бит готовности */
		while((RCC->CR & RCC_CR_PLLRDY) == 0) {
			// Ждем
		}

		/* Работает? Можно переключать! Выбираем PLL как источник системной частоты */
		RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
		RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;

		/* Ожидаем, пока PLL выберется как источник системной частоты */
		while((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) != (uint32_t)0x08) {
			// Ждем
		}
	} else {

		/* Все плохо... HSE не завелся... Чего-то с кварцем или еще что...
	 	Надо бы както обработать эту ошибку... Если мы здесь, то мы работаем
	 	от HSI! */
	}



	// Set up RCC on USART1 & GPIOA & GPIOC, set up alt func., set up RCC on ADC1
	RCC->APB2ENR|= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN;
	AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
	// Set up RCC on TIM2
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
}

void StartWork(void) {

	for(u8 i = 0; i < cycles; i++) {

		/* ! PRINT DATA ON DISPLAY 1602
		 *
		 *
		 */


		// Запускаем работу первой точки до тех пор, пока не кончится время
		while((timer*60) < setting_time[i]) {

			// Здесь поддерживаем заданную температуру
			if(PT100_GetTemp() < setting_temp[i]) {

				GPIOC->ODR |= (1<<HEATER_pin);
			}
			else {

				GPIOC->ODR &= ~(1<HEATER_pin);
			}
		}

		// Сбрасываем переменную времени и запускаем следующую точку
		timer = 0;
	}

	// После окончания всех режимов отключаем нагреватель и уходим в бесконечный цикл
	GPIOC->ODR &= ~(1 << HEATER_pin);
	for(;;);
}

u32 PT100_GetTemp(void) {

	// При 0С = 720, при 220С = 4095.
	return map(ADC1->DR, 720, 4095, 0, 220);
}

float map(float x, float in_min, float in_max, float out_min, float out_max) {

	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void WriteNewParams(const char *str) {

	// Записываем новые параметры во флеш
	for(u8 i = 0; i < cycles; i++) {

		// setting_temp[i] = uart_data;

		// Запись параметра температуры во флеш после записи о номере точек (adress page31 + 4byte)
		FLASH_Write(i*4 + (page31 + 4), (u32)setting_temp[i]);

		// setting_time[i] = uart_data;

		// Запись параметра времени во флеш после последней записи параметра температуры
		FLASH_Write(i*4 + (page31 + 4) + (cycles * 4), (u32)setting_time[i]);
	}
}