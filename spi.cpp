#include "spi.h"
#include <pthread.h>
#include <cstdio>
#include <unistd.h>
#include "miosix.h"

using namespace miosix;

//definisco i gpio che usiamo
typedef Gpio<GPIOA_BASE,1> interruptLine;//linea interrupt del modulo wireless
typedef Gpio<GPIOA_BASE,2> powerLine;//linea per alimentare il modulo wireless(da chiedere al prof se compatibile con altri moduli)
typedef Gpio<GPIOB_BASE,11> cen;//linea enable del modulo wireless
typedef Gpio<GPIOB_BASE,12> cs;//ss spi
typedef Gpio<GPIOB_BASE,13> sck;//clock spi
typedef Gpio<GPIOB_BASE,14> miso;//miso spi
typedef Gpio<GPIOB_BASE,15> mosi;//mosi spi

//prossime due funzioni sono l'interrupt handler
void __attribute__((naked)) EXTI1_IRQHandler(){

	saveContext();
	asm volatile("bl _Z16EXTI1HandlerImplv");
	restoreContext();

}

/*! @brief routine di interrupt scatenato dal modulo wireless
 * 	   verrà chiamato quando arriva un pacchetto o finisce la trasmissione
 */
void __attribute__((used)) EXTI1HandlerImpl(){

	EXTI->PR=EXTI_PR_PR1;

	//TO DO gestione interrupt del modulo wireless(arriva quando trasmissione finita o pacchetto ricevuto

}

/*! @brief configura la periferica spi2 e i gpio usati per la comunicazione con il modulo wireless
 */
void configureSpi(){

	//Abilito i clock alle periferiche che uso(spi2, gpioa e gpiob)
	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;//abilito il clock alla spi2
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;//abilito il clock ai gpio a e b che usiamo

	//imposto gpiob 12,13,14,15 per spi
	cs::mode(Mode::OUTPUT);
	sck::mode(Mode::ALTERNATE);
	miso::mode(Mode::ALTERNATE);
	mosi::mode(Mode::ALTERNATE);

	cs::high();
	sck::alternateFunction(5);
	miso::alternateFunction(5);
	mosi::alternateFunction(5);

	//imposto gpioa 2 per l'alimentazione
	powerLine::mode(Mode::OUTPUT);
	powerLine::low();

	//imposto gpiob 11 in output digitale per chip enable
	cen::mode(Mode::OUTPUT);
	cen::low();
	
	//configurazione gestione interrupt
	interruptLine::mode(Mode::INPUT_PULL_UP);
	SYSCFG->EXTICR[1] = SYSCFG_EXTICR1_EXTI1_PA;
	EXTI->IMR |= EXTI_IMR_MR1;
	EXTI->RTSR &= ~EXTI_RTSR_TR1;
	EXTI->FTSR |= EXTI_FTSR_TR1;
	NVIC_EnableIRQ(EXTI1_IRQn);
	NVIC_SetPriority(EXTI1_IRQn,15);

	SPI2->CR2 |= SPI_CR2_SSOE;//abilito l'uscita SS

	//imposto il control register 1
	SPI2->CR1 |= SPI_CR1_BR_0 | SPI_CR1_BR_1 | SPI_CR1_BR_2 ;//imposta a velocità di trasmissione a 2 MHz
	SPI2->CR1 &= ~SPI_CR1_CPHA;//impostato campionamento sul primo fronte di clock
	SPI2->CR1 &= ~SPI_CR1_CPOL;//impostato clock idle basso
	SPI2->CR1 &= ~SPI_CR1_DFF;//impostato frame da 8 bit
	SPI2->CR1 &= ~SPI_CR1_LSBFIRST;//impostato così a 0 manda prima msb
	SPI2->CR1 &= ~SPI_CR1_SSM;//il management del SS è hardware
	SPI2->CR1 |= SPI_CR1_MSTR;//imposto come master
	SPI2->CR1 |= SPI_CR1_SPE;//enable della spi

	return;

}

/*! @brief alza il pin per l'alimentazione del modulo wireless
 */
void powerLineUp(){
	powerLine::high();
}

/*! @brief abbassa il pin di alimentazione del modulo wireless
 */
void powerLineDown(){
	powerLine::low();
}

/*! @brief alza il bit di enable del modulo wireless
 * 	   Così il modulo può passare a rx o tx mode
 */
void chipEnable(){
	cen::high();
}

/*! @brief abbassa il bit di CE
 *         Il modulo wireless non può entrare in stato di rx o tx, solo standy o power-down
 */
void chipDisable(){
	cen::low();
}

/*! @brief manda via spi comando e dati al modulo wireless. Primitiva bloccante, non torna finchè errore o invio completo
 *  @param command comando da inviare
 *  @param addr indirizzo del registro del modulo wireless(se comando senza indirizzo usare COMMAND_WITHOUT_ADDRESS)
 *  @param sr puntatore a uint8_t dove verrà scritto lo status register ricevuto dal modulo wireless
 *  @param data puntatore ai dati da inviare(se ci sono) dopo il comando. Verranno inviati da data[0] a data[lenght-1].
 *  	   ATTENZIONE i dati sono LSByte
 *  @param lenght numero di byte(di dati) da inviare dopo il comando
 *  @return -1 se errore, 1 se inviato
 */
int spiSendCommandWriteData(uint8_t command, uint8_t addr,uint8_t* sr, uint8_t* data, int lenght){
	
	int i=0;
	uint16_t temp;

	if ( lenght > 0 && data == NULL ){//errore nell'uso dei parametri
		return -1;
	}

	temp = (uint16_t)command | (uint16_t)addr;

	while( ( SPI2->SR & SPI_SR_BSY ) != 0 ){//controllo che non sia occupata spi, se si ritorno errore
		i++;//i usato come timeout
		if(i>100){
			return -1;
		}
	}

	i=0;//i usato per tenere traccia del prossimo dato da inviare

	while( ( SPI2->SR & SPI_SR_TXE ) == 0 ){}//aspetto che registro trasmissione sia vuoto(probabilmente istruzione inutile)
	
	cs::low();//attivo il cs(lo abbasso)
	usleep(1);

	SPI2->DR = temp;//inserisco comando nel data register
	
	while( ( SPI2->SR & SPI_SR_TXE ) == 0 ){}//aspetto che sia copiato nel registro di invio

	if( lenght > 0 ){//inserisco nel data register il prossimo dato da inviare via spi(se c'è)
		temp = (uint16_t)data[i];
		i++;
		SPI2->DR = temp;
	}

	while( ( SPI2->SR & SPI_SR_RXNE ) == 0 ){}//aspetto che arrivi lo status register

	temp = SPI2->DR;//leggo lo status register

	if ( sr != NULL ){//se sr punta a un indirizzo di memoria valido passo lo status register
			*sr = (uint8_t)temp;
	}

	if( lenght == 0 ){//se non c'erano dati da inviare finito
		usleep(1);
		cs::high();//disabilito il cs(alzo cs)
		return 1;
	}

	while( i < lenght ){//c'è ancora da scrivere

		while( ( SPI2->SR & SPI_SR_TXE ) == 0 ){}//attendo che sia copiato il dato precedente dal DR allo shift register

		temp = (uint16_t)data[i];
		i++;
		SPI2->DR = temp;//inserisco prossimo dato nel DR

		while( ( SPI2->SR & SPI_SR_RXNE ) == 0 ){}//aspetto che arrivi la risposta al vecchio dato(sensa significato)

		temp = SPI2->DR;//leggo la risposta solo per non far andare in overrun(lettura non usata)

	}

	while( ( SPI2->SR & SPI_SR_RXNE ) == 0 ){}//aspetto la trasmissione dell'ultimo dato

	temp = SPI2->DR;

	usleep(1);
	cs::high();//disabilito cs(lo alzo)

	return 1;

}

/*! @brief manda via spi comando e legge dati forniti dal modulo wireless. Primitiva bloccante, non torna finchè errore o invio completo
 *  @param command comando da inviare
 *  @param addr indirizzo del registro nel modulo wireless(se comando senza indirizzo usare COMMAND_WITHOUT_ADDRESS)
 *  @param sr puntatore a uint8_t dove verrà scritto lo status register ricevuto dal modulo wireless
 *  @param data puntatore ai dati ricevuti dopo il comando. Verranno riempiti da data[0] a data[lenght-1] in ordine di ricezione.
 *  	   ATTENZIONE la funzione non controllerà se la zona di memoria non è stata allocata correttamente per contenere 
 *  	   tutti i dati attesi e si ricorda che come per la primitiva spiSendCommandWriteData i dati sono LSByte
 *  @param lenght numero di byte(di dati) da ricevere dopo aver inviato il comando
 *  @return -1 se errore, 1 se inviato
 */
int spiSendCommandReadData(uint8_t command, uint8_t addr,uint8_t* sr, uint8_t* data, int lenght){
	
	int i=0;
	uint16_t temp;

	if ( lenght > 0 && data == NULL ){//errore nell'uso dei parametri
		return -1;
	}

	temp = (uint16_t)command | (uint16_t)addr;

	while( ( SPI2->SR & SPI_SR_BSY ) != 0 ){//controllo che non sia occupata spi, se si ritorno errore
		i++;//i usato come timeout
		if(i>100){
			return -1;
		}
	}

	i=0;//i usato per tenere traccia del numero di dati ricevuti

	while( ( SPI2->SR & SPI_SR_TXE ) == 0 ){}//aspetto che registro trasmissione sia vuoto(probabilmente istruzione inutile)

	cs::low();//abilito il cs(lo abbasso)
	usleep(1);

	SPI2->DR = temp;//inserisco comando nel data register
	
	while( ( SPI2->SR & SPI_SR_TXE ) == 0 ){}//aspetto che sia copiato nel registro di invio

	if( lenght > 0 ){//siccome devo ricevere dati invio dati senza senso via spi
		temp = 0x0000;
		i++;
		SPI2->DR = temp;
	}

	while( ( SPI2->SR & SPI_SR_RXNE ) == 0 ){}//aspetto che arrivi lo status register

	temp = SPI2->DR;//leggo lo status register

	if ( sr != NULL ){//se sr punta a un indirizzo di memoria valido passo lo status register
			*sr = (uint8_t)temp;
	}

	if( lenght == 0 ){//se non c'erano dati da inviare finito
		usleep(1);
		cs::high();//disabilito cs(lo alzo)
		return 1;
	}

	while( i < lenght ){//c'è ancora da leggere

		while( ( SPI2->SR & SPI_SR_TXE ) == 0 ){}//attendo che sia copiato il dato precedente dal DR allo shift register

		temp = 0x0000;
		i++;
		SPI2->DR = temp;//inserisco prossimo dato nel DR

		while( ( SPI2->SR & SPI_SR_RXNE ) == 0 ){}//aspetto che arrivi la risposta precedente

		temp = SPI2->DR;//leggo la risposta precedente

		data[i-2] = (uint8_t)temp;
	}

	while( ( SPI2->SR & SPI_SR_RXNE ) == 0 ){}//aspetto la ricezione dell'ultimo dato

	temp = SPI2->DR;

	usleep(1);
	cs::high();//disabilito cs(lo alzo)

	data[i-1] = (uint8_t)temp;

	return 1;

}
