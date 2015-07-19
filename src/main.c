//
//		INTRODUCAO A DIGITAL POWER COM A STM32F334
//
//		@file  main.c
//      @brief implementa uma engine de controle de brilho de LED closed loop
//             sem usar a CPU.
//



#include "stm32f30x.h"
#include "stm32f3348_discovery.h"
#include "stm32f30x_rcc.h"
#include "stm32f30x_comp.h"
#include "stm32f30x_hrtim.h"
#include "stm32f30x_gpio.h"
#include "stm32f30x_dac.h"

//
// Macros e constantes uteis:
//

#define TICK_FREQ      1000   //frequencia de updade do tick.
#define SYSTICK_LOAD_VAL(x) (x == 0?0:(SystemCoreClock) /x)    //recarga do systick


#define BUTTON_SCAN_TIME 10   //periodo de scanning do botao em ms


#define HRTIM_FREQ     250000 //Frequencia do PWM em HZ
#define HRTIM_LOAD_VAL(x) 	(x == 0?0:((SystemCoreClock/x) * 64)) //VAlor de recarga do HRTIM

#define DIMMING_MAX_VAL  400   //corresponde a corente de 350mA no led;
#define DIMMING_MIN_VAL   10   //corresponde a um valor de baixa luminosidade.

//
// maquininha simples de estado do dimmer:
//
typedef enum
{
	kdimmingUp = 0,
	kdimmingDown,
}dim_state;

//
// Variaveis:
//
uint16_t bright = 0;
uint32_t tickCounter = 0;
dim_state dimmingMchn = kdimmingUp;

//
// HRTIM_Init()
// @brief inicializa o hrtim em modo PWM
//
//
static void HRTIM_Init(uint32_t freq)
{
	GPIO_InitTypeDef gpio;
	gpio.GPIO_Pin  = GPIO_Pin_12 ;
	gpio.GPIO_Mode  = GPIO_Mode_AF;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_Level_3;

	//Aciona o clock do HRTIM:
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_HRTIM1, ENABLE);
	RCC_HRTIM1CLKConfig(RCC_HRTIM1CLK_PLLCLK);

	//Aciona o pino que queremos o HRTIM:
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
	GPIO_Init(GPIOB, &gpio);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource12,GPIO_AF_13);

	//Configura o HRTIM para rodar da segguinte forma:
	// fpwm  = 250KHz -- 15bits
	// set   = CMP1
	// reset = PER1 / CMP4 - EEV2
	HRTIM1->HRTIM_MASTER.MCR  = 0x00000000; //Derruba o master timer:
	HRTIM1->HRTIM_MASTER.MPER = HRTIM_LOAD_VAL(freq); //Acerta o periodo
	HRTIM1->HRTIM_TIMERx[2].TIMxCR = 0x00000000; //derruba o timer a ser usado
	HRTIM1->HRTIM_TIMERx[2].PERxR  = HRTIM_LOAD_VAL(freq); //Acerta o periodo
	HRTIM1->HRTIM_TIMERx[2].CMP1xR = 14000; //Duty cicle inicial = 0
	HRTIM1->HRTIM_TIMERx[2].SETx1R = 0x04; // quem faz set
	HRTIM1->HRTIM_TIMERx[2].RSTx1R = 0x08 | (1 << 22); //quem faz reset
	HRTIM1->HRTIM_TIMERx[2].OUTxR  = 0x00; //seleciona a polaridade.
	HRTIM1->HRTIM_COMMON.OENR      = 0x3FF;//habilita as saidas PWM.

	HRTIM1->HRTIM_TIMERx[2].TIMxCR = 0x08; //dispara o TIMERC em PWM
	HRTIM1->HRTIM_MASTER.MCR  = 0x003F0008; //Habilita o master timer

	//Configura as fontes de eventos externos:
	HRTIM1->HRTIM_COMMON.EECR1  = (0x01 << 6);
	HRTIM1->HRTIM_COMMON.EECR1 |= (0x01 << 10);

}


//
// AFE_Init()
// @brief Inicializa o front-end de medida de corrente:
//
//
static void AFE_Init(void)
{
	GPIO_InitTypeDef gpio;
	COMP_InitTypeDef cmp;
	DAC_InitTypeDef  dac;

	//
	// Coloca o GPIO que sera usado como comparador num estado conhecido
	//

	gpio.GPIO_Pin  = GPIO_Pin_6 ;
	gpio.GPIO_Mode  = GPIO_Mode_AN;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_Level_3;


	//Configura o gpio que sera usado como Comparador
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
	GPIO_Init(GPIOA, &gpio);

	gpio.GPIO_Pin  = GPIO_Pin_0 ;
	gpio.GPIO_Mode  = GPIO_Mode_AN;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_Level_3;


	//Configura o gpio que sera usado como Comparador
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
	GPIO_Init(GPIOB, &gpio);


	gpio.GPIO_Pin  = GPIO_Pin_1 ;
	gpio.GPIO_Mode  = GPIO_Mode_AF;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_Level_3;

	GPIO_Init(GPIOB, &gpio);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource1,GPIO_AF_8);

	//Configura o comparador:
    COMP_StructInit(&cmp);
	cmp.COMP_Mode = COMP_Mode_HighSpeed;
	cmp.COMP_BlankingSrce = COMP_BlankingSrce_None;
	cmp.COMP_InvertingInput = COMP_InvertingInput_DAC2OUT1;
	cmp.COMP_NonInvertingInput = COMP_NonInvertingInput_IO1;
	cmp.COMP_Output = COMP_Output_HRTIM1_EE2_2;
	cmp.COMP_OutputPol = COMP_OutputPol_NonInverted;


	//Inicializa o comparador:
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	COMP_Init(COMP_Selection_COMP4,&cmp);
	COMP_Cmd(COMP_Selection_COMP4, ENABLE);


	//Configura o DAC:
	dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
	dac.DAC_Trigger        = DAC_Trigger_Software;
	dac.DAC_Buffer_Switch  = DAC_BufferSwitch_Enable;

	//Inicializa e habilita o DAC:
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC2, ENABLE);
	DAC_Init(DAC2, DAC_Channel_1, &dac);
	DAC_SoftwareTriggerCmd(DAC2,DAC_Channel_1, ENABLE);
	DAC_Cmd(DAC2, DAC_Channel_1, ENABLE);

}
//
// DAC_Update()
// @brief Atualiza o valor do DAC utilizado como Vref do conversor:
//
static inline void DAC_Update(uint16_t dacVal)
{
	if(dacVal > 4095) dacVal = 4095;
	DAC_SetChannel1Data(DAC2, DAC_Align_12b_R, dacVal);
	DAC_SoftwareTriggerCmd(DAC2,DAC_Channel_1, ENABLE);
}

//
// HRPWM_Update
// @brief atualiza o duty-cicle base do gerador de pwm
//
static inline void HRPWM_Update(uint16_t pwmVal)
{
  HRTIM1->HRTIM_TIMERx[2].CMP1xR = pwmVal;
}

//
// main()
// @brief funcao principal, captura o estado do botao e regula
//        o brilho do led.
//
int main(void)
{
	uint32_t scan = 0;

	//
	// Inicializa o analog front end,
	// esse bloco eh quem vai agir como loop control
	//
	AFE_Init();
	DAC_Update(DIMMING_MIN_VAL);
	//
	// Sobe o pwm de alta resolucao, esse bloco vai trabalhar
	// em conjunto com o AFE aplicando um fator de
	// correcao ao PWM permitindo uma correcao ciclo a ciclo
	// em caso de step de carga ou elevacao de tensao.
	HRTIM_Init(HRTIM_FREQ);

	//
	//Configura o botao da discovery para ser usado como controle
	//de dimmer.
	//
	STM_EVAL_PBInit(BUTTON_USER,BUTTON_MODE_GPIO);

	//
	// Configura o systick counter para gerar uma
	// base tempo constante.
	//
	//
	SysTick->CTRL = 0x00;
	SysTick->LOAD = SYSTICK_LOAD_VAL(TICK_FREQ);
	SysTick->CTRL = 0x07;

	scan = tickCounter;

	for(;;)
	{
		//Escaneia o botao da placa a cada 10ms
		if(tickCounter - scan >= BUTTON_SCAN_TIME)
		{
			//Checa se o botao foi pressionado:
			if(STM_EVAL_PBGetState(BUTTON_USER) != 0)
			{
				//Avalia a maquininha de estados:
				switch(dimmingMchn)
				{
					case kdimmingUp:

						bright++;
						if(bright > DIMMING_MAX_VAL)
						{
							//Realiza wrap e troca de estado do dimmer
							dimmingMchn = kdimmingDown;
							bright = DIMMING_MAX_VAL;
						}
					break;


					case kdimmingDown:
						bright--;
						if(bright <= DIMMING_MIN_VAL)
						{
							//Realiza wrap e troca de estado do dimmer
							dimmingMchn = kdimmingUp;
							bright = DIMMING_MIN_VAL;
						}
					break;
				}

				//
				// Depois da maquina de estado avaliada
				// Atualizamos o valor de brilho desejado no led
				// pelo DAC, a engine configurada no hardware se
				// encarrega de buscar o brilho desjeado pelo usuario.
				//
				DAC_Update(bright);
			}

		    scan = tickCounter;
		}
	}
}

//
// SysTick_Handler()
// @brief esta interrupcao atualiza o contador de
//        base de tempo.
void SysTick_Handler(void)
{
	tickCounter++;
}
