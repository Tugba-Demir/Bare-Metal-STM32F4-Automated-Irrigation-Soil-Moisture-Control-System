#include "main.h"
#include <stdbool.h>
#include <string.h>

void SystemClockConfigUpdate(void);
void GPIO_Config(void);
void TIM2_Config(void);
void NVIC_Config(void);
void ADC_Config(void);
void UART4_Config(void);
void DMA2_Config(void);
void DMA1_Config(void);

// 12-bit ADC için Eşik Değerleri (0 - 4095 arası)
#define ESIK   3000  // Bu değerin üstü KURU -> suyu aç, altı suyu kapa

// Servo Pulse (CCR2) Değerleri
#define SERVO_POS_1 490 // su açılacakken olması gereken konum
#define SERVO_POS_2   2500 // Su kapanacakken olması gereken konum

volatile uint16_t adc_val;
volatile bool su_acik_mi=false; // su kapali
char adc_val_karakterleri[16]; // UART4 Tx pininden(PortC_10) gönderilecek char dizisinin boyutu max 16 byte olsun
extern volatile uint8_t dma_tx_busy;

int main(void)
{
	// Eğer HAL_Delay i kullanacaksak NVIC e interrupt isteğini enable etmeyi unutma, aksi takdirde HAL_Delay çağırıldığında kendi döngüsünde takılı kalır

	SystemClockConfigUpdate();
	GPIO_Config();
	TIM2_Config();
	NVIC_Config();
	DMA2_Config();
	DMA1_Config();
	UART4_Config();
	ADC_Config();

	//TIM2->CCR2 = x; // CCR bizim pwm sinyalinin bir pulse ında lojik1 seviyesinde kalma değerini(süresini) belirlememizi sağlar. Ben TIM2 kullandığım için
				      // 32 bit in tamamını kullanabilirim. TIM5 kullansaydım onda da 32 bitin tamamını kullanabilirdim. Örneğin x değeri
					  // 0.5 milisaniyeye tekabül etsin. Yani 0.02 saniyelik pulse ın 0.5 milisaniyesi boyunca lojik 1 de kalır.
					  // Bu da servonun içerisindeki çip için x derece hareket et demek. Yani her Dutycycle değerinin(CCR) karşılığı olan bir derce değeri var.
					  // Bu duty değerine göre servo o konuma geliyor.

	// Program başlatıldığında ilk konum olarak SERVO_POS_1 konumuna gelsin servo
	int i=0;
	while(i<=SERVO_POS_1){
		TIM2->CCR2 = i; // su kapalı olmalı
		HAL_Delay(20);
		i+=50;
	}

	while (1)
	{
		// 1. Durum: Toprak KURUDU ve Servo henüz açılmadıysa
		if ((adc_val > ESIK) && (su_acik_mi==false)) // toprak nem oranı düşükse ve su kapalıysa suyu aç
		{
			for(int i=SERVO_POS_1; i<=SERVO_POS_2; i+=50){
				TIM2->CCR2 = i;
				HAL_Delay(20);
			}

			su_acik_mi = true; // su artık açık

		}
		// 2. Durum: Toprak YETERİNCE NEMLENDİ ve Servo açıksa
		else if((adc_val <= ESIK) && (su_acik_mi==true))
		{
			for(int i=SERVO_POS_2; i>=SERVO_POS_1; i-=50){
				TIM2->CCR2 = i;
				HAL_Delay(20);
			}
			su_acik_mi = false; // su artik kapali
		}


		if(!dma_tx_busy) // karakter dizisi gönderimi tamamlanana, yani NDTR=0 olana kadar bu if bloğuna girilmez.
		{
			// Gönderilecek yeni karakter dizisini hazırla
			sprintf(adc_val_karakterleri, "Nem: %u\r\n", (unsigned long)adc_val);

			// Stream'i kapat ve bekle. Çünkü NDTR Stream açıkken güncellenemez ve zaten yeni karakter dizisini hazırlarken stream in durdurulması gerek
			DMA1_Stream4->CR &= ~DMA_SxCR_EN;
			while(DMA1_Stream4->CR & DMA_SxCR_EN);

			// Yeni uzunluğu yaz ve adresi garantiye al
			DMA1_Stream4->NDTR = strlen(adc_val_karakterleri);

			// Meşgul bayrağını kaldır ve Stream'i başlat
			dma_tx_busy = 1;
			DMA1_Stream4->CR |= DMA_SxCR_EN;
		}

		HAL_Delay(200);

	}
  /* USER CODE END 3 */
}


void UART4_Config(){

	// İletişim STM32F4 ten bilgisayara doğru nem bilgisini göndermek olacak. O yüzden sadece transmit işlemi olacak

	RCC->APB1ENR |= (1<<19); // UART4 ün bağlı olduğu 42MHz frekansta çalışan APB1 hattını UART4 için anable et.


	//*********************** UART4_CR1 ***********************
	UART4->CR1 |= (1<<15); // Oversampling by 8. Yani bir bit süresi boyunca 8 defa örnekleme yapılacak.
	UART4->CR1 &= ~(1<<12); // "1 bit Start" + " 8 Data Bits" + "n Stop bits"

	// NOT: STM32 zaten master olarak kullanılacağı için wakeup method ayarlanmayacak, parity kullanılmayacağı için
	//      de PS(parity selection) ve PEIE(parity error interrupt enable) biti konfigüre edilmeyecek


	UART4->CR3 &= ~(1<<3); // Full duplex iletişim olacak ve receiver ı disable edip sadece transmitter i enable edeceğiz
	UART4->CR1 |= (1<<3); // Transmit yapacağımız için sadece Tranmitter i anble ettim, receiver ı etmeyeceğim
	UART4->CR1 &= ~(1<<2); // Receiver disable


	// *********************** UART4_CR2 ***********************
	UART4->CR2 &= ~(3<<12); // STOP biti = 1 bit

	// NOT: Diğer ayarlar clok konfigürasyonu için ama asenkron iletişim olacağı için onları konfigüre etmeye gerek yok

	/* NOT: ADD bitinin amacı: USART biriminin Çoklu İşlemci Haberleşme Modunda (Multi-Processor
	        Communication / Mute Mode) kullandığı benzersiz Cihaz Adresini (Node Address) belirlemek için kullanılır.
	        Yani ADD bitleri bir hatta 1 master ve birden fazla slave olduğunda master ın hangi slave ile iletişime
	        geçeceğinin anlaşılması için slave cihazın kullanacağı adres. Ancak Tek cihazlı sistemlerde hat üzerinde
	        adres karmaşası olmadığı için bu bit varsayılan 0000 değerinde bırakılır.
	*/


	// *********************** UART4_CR3 ***********************
	UART4->CR3 &= ~(1<<11); // oversampling yapılırken eğer sadece 3 tane clock darbesinin denk geldiği lojik değerleri karşılaştıracaksak
	                        // bu bit 0 yapılmalı(bu karşılaştırılacak darbeler sabit. Yani örneğin oversmapling by 8 için
							// 4, 6 ve 8. clock darbelerinde örneklenen lojik değerleri kıyaslar). Eğer sadece bir clock darbesinin geldiği
							// lojik seviyeye bakacaksak da bu bit 1 ayarlanmalı. Eğer 1 olursa da o bit bitlik sürede sadece  4. clock
							// darbesinde örnekleme olur(oversamling by 8 için ve bu sabit bir clock darbesi, yani illa 4. clock darbesinde örneklenecek)

	UART4->CR3 |= (1<<7); // Transmitter için DMA enable edilecek.


	/* Baud rate; saniyede kaç bitin gönderileceği veya alınacağını belirleyeceğimiz kısımdır. Bize user manuel de
	              verilen örnek tablolarda BRR register ını kaç ayarlarsak hangi hıza ulaşılır, ne kadar hata olur
	              gösteriliyor. Bizim;

	              Hedef hız= 115,2 KBps
	              Gerçekte: 115.068KBps
	              Bu hız için BRR ye yazılacak sayısal değer: 45(mantissa) + 625(fraction)
	              Hata payı(hedeflenen ile gerçekteki hız arasındaki hata): %0.11
	 */


	// Baud rate değeri=115.2 KBps hızı için tabloya göre ayarlanacak sayısal değerler:
	// 45,625 => tam kısım(mantissa)=45, kesirli kısım(fraction)=625
	UART4->BRR = (45 << 4) | 5; // veya (45 << 4) | 5;

	UART4->CR1 |= (1<<13); // UART4 enable et
}

void DMA1_Config(){

	// Akış: Memory->DMA1->UART4_Tx(PortC_10)
	// UART4_Tx pini DMA1 ile iletişime geçecekse DMA1 in Stream4 ünün Channel4 ünü konfigüre etmeliyim

	RCC->AHB1ENR |= (1 << 21); // DMA1 Clock Enable (Bit 21)

	// Önce strema i kapat.
	DMA1_Stream4->CR &= ~(1 << 0);
	while(DMA1_Stream4->CR & (1 << 0)); // Kapanmasını bekle

	// Stream4 ün channel4 ünü kullanacağımız için onu seçelim;
	DMA1_Stream4->CR &= ~(7<<25); // bitleri temizle
	DMA1_Stream4->CR |= (4<<25); // 100 olarak ayarladık



	// MSIZE ve PSIZE 8 bit olacak.(ADC value değeri 12 bit ama elde edilen adc_val sayısal değeri ben adc_val_karakterleri karakter dizisine
	// ekleyip onunla birlikte karakter karakter göndereceğim UART dan bilgisayara. Giderken karakter karakter gönderileceği için her
	// karakterin boyutu 8 bit. Örn: 1278 sayısal değeri; '1', '2', '7', '8' karakterlerinin
	// 8 bitlik ASCII leri gönderilecek.) Yani Memory den peripheral(UART4->DR) e 8 bit veri gidecek
	DMA1_Stream4->CR &= ~(3<<13); // MSIZE içn 8 bit olarak ayarlandı
	DMA1_Stream4->CR &= ~(3<<11); // PSIZE için 8 bit olarak ayarlandı

	// Priority si very high olsun. zaten farklı DMA ları kullandıkları için sorun olmaz. Eğer aynı DMA ları kullansalardı
	// stream leri farklı olacağı için aralrındaki öncelikleri farklı yapmak gerekebildir.
	DMA1_Stream4->CR |= (3<<16); // priority=11

	// PINC=0 ve MINC=1. Yani erişilen bellek alanları memory taraf için 8 bit artacak, çünkü karakter tutuyor ancak
	// UART4 için aynı kalacak. Reset te bu olduğu için bir ayarlama yapmıyorum PINC için
	DMA1_Stream4->CR |=  (1 << 10); // MINC=1

	DMA1_Stream4->CR &= ~(1<<8); // circular değil, normal mode u(tek seferlik aktarım) kullanmalıyız çünkü gönderilecek metin uzunluğu
								 // her defasında değişebilir ve bu durumda NDTR de değişebilir

	// Memory deki karakter dizisindeki her karakteri sırasıyla gönderirken adres her defasında artacak ama kaçar kaçar artacak o da önemli.
	// Kaçar kaçar artacağını da MSIZE boyutu belirler zaten. Bunun için ekstra bir ayara gerek yok


	DMA1_Stream4->CR &= ~(1<<8); // circular değil, normal mode u(tek seferlik aktarım) kullanmalıyız çünkü gönderilecek metin uzunluğu
								 // her defasında değişebilir ve bu durumda NDTR de değişebilir


	// "NDTR= adc_val_karakterleri karakter uzunluğu" yapacağım. Bu dizi UART dan gönderilirken NDTR 1 azalır.
	DMA1_Stream4->NDTR = strlen(adc_val_karakterleri); // Normalde adc_val_karakterleri dizisinin uzunluğu 16 byte olarak ayarlandı ama
													   // strlen dizinin için de \0 karakterini gördüğü yere kadar hesaplar uzunluğu. böylece boşuna fazla byte gönderimi olmaz.
													   // Bu yüzden main de de her yeni karakter dizisi göndereleceği zaman NDTR güncellenir

	// Data nın aktarım yönü: Memory->UART4_DR buffer
	DMA1_Stream4->CR &= ~(3<<6); // bitleri temizle
	DMA1_Stream4->CR |= (1<<6);

	// PFCTRL=0 kalsın. Çünkü iletişimi DMA yönetecek

	// DMA nın memory den aldığı veriyi taşıyacağı belleğin adresi = UART4_DR
	DMA1_Stream4->PAR = (uint32_t)&(UART4->DR);

	// DMA nın alacağı verinin bulunduğu adres
	DMA1_Stream4->M0AR = (uint32_t)adc_val_karakterleri;// adc_val_karakterleri bir dizi ismi ve dizi ismi dizinin başlangıç adresini verir


	DMA1_Stream4->CR |= (1 << 4); // TCIE = 1 (Transfer Complete Interrupt Enable)

	// Stream enable;
	DMA1_Stream4->CR |= (1<<0);

}


void DMA2_Config(){

	// DMA2_S0CR register ayarları:
	// Not: ADC1, DMA2 nin Stream0 ının Channel0 ı na bağlıdır(Table DMA2 request mapping). Bu yüzden DMA2 için konfigürasyon yapılacak


	RCC->AHB1ENR |= (1 << 22); // DMA2 Clock Enable (Bit 22)

	// Önce strema i kapat.
	DMA2_Stream0->CR &= ~(1 << 0);
	while(DMA2_Stream0->CR & (1 << 0)); // Kapanmasını bekle

	// *************************DMA_SxCR ayarları*************************
	// CHSEL = 000 ayarla
	DMA2_Stream0->CR &= ~(7<<25); // Stream0 ın CHSEL bitlerini 000 ayarlayarak channel0 ı kullanacağımızı belirttik.

	// MBURST=00 ve PBURST=00 olarak ayarlayarak her ADC verisi elde edildiğinde DMA dan RAM deki belleğe
	// aktarırız, yani her seferde 16 bit transfer olacak. Yani burst yapmıyor ki DMA birkaç 16-bit(PSIZE ve MSIZE 16 bit seçildi)t bekleyip topluca RAM e aktarmasın.
	// Çünkü bize 12-bit veri lazım, o yüzden beklemeye gerek yok
	DMA2_Stream0->CR &= ~(3<<23);
	DMA2_Stream0->CR &= ~(3<<21);

	// DBM=0, yani double buffer kullanmak istemediğimiz için 0 yap
	DMA2_Stream0->CR &= ~(1<<18);


	// PL=11 ayarlayarak stream0 ın channel0 ını very high priority olarak ayarla
	DMA2_Stream0->CR |= (3<<16);

	// PINC ve MINC i 0 olarak ayarlayarak her veri aktarımından sonra yine aynı hedef adreslerine veri
	// aktarılacağını söylüyoruz hem peripheral e hem de memory ye
	DMA2_Stream0->CR &= ~((1 << 10) | (1 << 9)); //

	// ADC den okunacak verinin boyutu 12-bit. Bu yüzden PSIZE ve MSIZE 16 bit olarak seçilecek. 01 ayarla
	// ADC DMA Konfigürasyonu (DMA2)
	DMA2_Stream0->CR &= ~(3 << 13); // MSIZE temizle
	DMA2_Stream0->CR |=  (1 << 13); // MSIZE = 16-bit (01)

	DMA2_Stream0->CR &= ~(3 << 11); // PSIZE temizle
	DMA2_Stream0->CR |=  (1 << 11); // PSIZE = 16-bit (01)

	/* NOT: Circular mode un amacı:  DMA veriyi RAM'e yazıp sayaç (NDTR) 0'a ulaştığında kendi kendini donanımsal olarak otomatik YENİDEN BAŞLATIR (Auto-reload)(Eğer circular mode yerine
	        Normal mode olsaydı NDTR değeri 0 olunca DMA direkt kapanırdı, ama circular mode da NDTR değeri tekrar N den 0 a kadar saymaya başlar). Sayacı tekrar N yapar,
	        adres işaretçisini (pointer) başa sarar ve sonsuz bir döngüde çalışmaya devam eder. O halde bu durumda NDTR=1 yapmak yeterli, çünkü NDTR her 0 olduğunda tekrar otomatik değer yüklenip
	        DMA kapanmayacak zaten. Ama yine de NDTR=N deyip circular mode da kullanabiliriz. sadece bence NDTR yi her defasında N kadar sayım yapmaktan kurtarırız. Çünkü zaten bu nem bilgisini
	        program çalıştığı sürece sürekli almak istiyoruz. O yüzden NDTR yi 1 yapıp circular mode yapmak daha doğru.
	 */
	// Circular mode u analbe et. yani 1 yap.
	DMA2_Stream0->CR |= (1<<8);


	// Verinin akatarılacağı yön Peripheral den Memory ye. DIR bitleri=00
	DMA2_Stream0->CR &= ~(3<<6);

	// PFTRL Notu:
	/*
	   PFCTRL = 0 (Flow Controller = DMA):Transferin ne zaman biteceğini DMA belirler.Sen koda NDTR = 1 veya NDTR = 10 yazarsın. DMA,
	   kaç adet veri taşıyacağını NDTR sayacından bilir. N adet veri taşındığı an işlemi bitirir veya başa sarar(circular mode ve normal mode a bağlı).
	   Çevre biriminin (ADC, SPI vb.) kaç veri göndereceğinden haberi yoktur, kontrol tamamen DMA'dadır.

	   PFCTRL = 1 (Flow Controller = Peripheral):Transferin ne zaman biteceğini Çevre Birimi (Peripheral) belirler.DMA kaç tane veri taşıyacağını bilmez!
	   NDTR sayacını kullanmaz veya dikkate almaz. Çevre birimi donanımsal olarak "Benim gönderecek paketim bitti/paket sonu sinyali (End of Packet)" diyene kadar DMA veriyi çekmeye devam eder.

	 */
	DMA2_Stream0->CR &= ~(1<<5); // Ben akışın kontrolü DMA da olsun istiyorum. O yüzden PFCTRL=0 yaptım


	// *************************DMA_SxNDTR ayarları*************************
	DMA2_Stream0->NDTR = 1; // NDTR her veri taşıma sonucunda 1 azalır ve 0 olunca Normale mode da DMA kapanır. Zaten circular mode kullandığım için NDTR ye 1 demem yeterli, yani NDTR=0 olunca DMA kapanmaz.


	// *************************DMA_SxPAR ayarları*************************
	DMA2_Stream0->PAR = (uint32_t)&(ADC1->DR); // DMA nın alacağı verinin hangi adresten alacağını belirttim. Yani ADC1 in DR sinden alacak

	// *************************DMA_SxM0AR ayarları*************************
	DMA2_Stream0->M0AR = (uint32_t)&adc_val;

	// NOT: FIFO kullanmayacağım için Direvt mode enabled, durumda yani FIFO disable. Hiç FIFO buffer ında birikme yapmadan ADC den Memory ye veri taşınıyor

	// Stream i başlat. EN=1 yap
	DMA2_Stream0->CR |= (1<<0);
}

void TIM2_Config(void)
{

	// TIM2 PWM sinyali üretebilen bir General timer dır. Bu timer ı PortA_1 pininden dış dünyaya(servo PWM pinine) PWM sinyali gönderebilmek için konfigüre edeceğim.
	// TIM2 nin CH2 kanalı kullanılacak. Yani; TIM2_CH2 -> PortA_1 nolu pin

	RCC->APB1ENR |= (1<<0); // TIM2 için bağlı olduğu APB1 hattını enable ettim.

    // Prescaler ve Auto-Reload Ayarları
    TIM2->PSC = 83;                     // 84MHz / 84 = 1MHz (1us counter)
    TIM2->ARR = 19999;                  // 20000 * 1us = 20ms (50Hz) = 0.02 sn lik pulse lar üretiliyor

    // CCMR1 Ayarları (Kanal 2 Çıkış ve PWM Mode 1)
    TIM2->CCMR1 &= ~(3 << 8);           // CC2S[1:0] = 00 (Output Mode)
    TIM2->CCMR1 &= ~(7 << 12);          // OC2M[2:0] temizle
    TIM2->CCMR1 |=  (6 << 12);          // OC2M[2:0] = 110 (PWM Mode 1)
    TIM2->CCMR1 |=  (1 << 11);          // OC2PE = 1 (Preload Enable)

    // CCER Ayarı (Channel 2 Output Enable)
    TIM2->CCER |= (1 << 4);             // CC2E = 1. CH2 kanalı kullanılacağı için CH2 kanalını enable ettim.

    // CR1 Ayarları (CEN Hariç)
    TIM2->CR1 |=  (1 << 7);             // TIM2->ARR = x yazdığımızda bu değer doğrudan aktif kaydediciye yazılır. ARPE = 1 (Auto-Reload Preload Enable)
    									// yaptığımızda ise yazdığımız yeni ARR değeri önce Preload (Shadow Öncesi) kaydediciye alınır.
    									// Counter taşma (Update Event) yapana kadar bekler ve taşma anında Gölge (Shadow) kaydediciye aktarılır. Bu durum,
    									// timer çalışırken ARR değerini değiştirirsek sinyalde bozulma (glitch) olmasını engeller.
    TIM2->CR1 &= ~(1 << 4);             // DIR = 0 (Upcounter)
    TIM2->CR1 &= ~(1 << 2);             // URS = Güncelleme kesmesi/DMA isteği; hem overflow/underflow, hem UG biti (yazılımsal), hem de slave mod kontrolcüsü tarafından tetiklenebilir.
    TIM2->CR1 &= ~(1 << 1);             // UDIS = 0. Counter dolup başa sardığında (overflow/underflow) veya sen koda UG = 1 yazdığında donanım otomatik olarak Shadow
    									// register'ları günceller ve kesme/DMA bayrağını kaldırır.


    // Register Değerlerini Shadow Register'lara Yükleme (UG = 1)
    TIM2->EGR |= (1 << 0);              // UG = 1 (Update Event Üret). Timer başlatıldıktan sonra her overflow/underflow da shadow register larından register lara yükleme yapılacak ama başlangıçta
    									// güncelleme için overflow/underflow olmadığından bu güncellemeyi UG biti ile yazılımsal olarak yaptım

    while (!(TIM2->SR & (1 << 0)));     // UIF (Bit 0) set olana kadar bekle (Shadow yüklemesi tamamlansın)
    TIM2->SR  &= ~(1 << 0);             // UG sonucu kalkan UIF bayrağını temizle

    // Timer'ı Başlat (CEN = 1)
    TIM2->CR1 |= (1 << 0);              // CEN = 1
}

void ADC_Config(){

	// ADC PortC nin 1. pinine bağlı olduğu için 11. kanaldan gelen analog verinin conversion ununu gerçekleştirmek için
	// yapılandırılacak.

	// ADC1 in bağlı olduğu APB2 bus ını aktif et
	RCC->APB2ENR |= (1<<8);

	// ADC1 in çalışacağı frekans bağlı olduğu busın(APB2) 1/8 i kadar olacak. Çünkü çalışma frekansı ne kadar az olursa ölçümün doğru olma ihtimali daha çok olur
	// Not: CCR register ı tüm ADC ler için ortak. Yani bu APB2/8 çalışma frekansı tüm ADC ler için geçerli. Eğer ADC lerden birinin çok hızlı çevrime ihtiyacı varsa o zaman ona göre prescaler ı seçmen daha doğru olur
	ADC->CCR &= ~(3<<16);
	ADC->CCR |= (3<<16);

	// Resolution = 12-bit çözünürlük. Yani Analog değeri 12 bitlik dijital alana sığdıracağız
	ADC1->CR1 &= ~(3<<24); // 12 bit çözünürlük

	// Data alignment = right alignment
	ADC1->CR2 &= ~(1<<11);

	// continuous conversion u enable et
	ADC1->CR2 |= (1<<1);

	// PortC nin 1. pini 11. kanala bağlı olduğu için SMPR1 register ından 84 cycles ayarla sample süresini
	ADC1->SMPR1 &= ~(7<<3);
	ADC1->SMPR1 |= (4<<3);

	// Daha sonra buraya ADC low ve high threshold değerlerini de gir.
	ADC1->LTR = 1000; // Okunan değer 1000 in altına inmişse artık nem miktarı çok fazladır. o yüzden uyarı al
	ADC1->HTR = 3000; // Okunan değer 3000 in üzerindeyse artık nem miktarı çok azdır. o yüzden uyarı al.

	// Watchdog un uygulanacağı kanalı 11 olarak seç(11=01011)
	ADC1->CR1 &= ~(0x1F<<0); // bitleri temizle
	ADC1->CR1 |= (11<<0);// 11 yap

	// Analog watchdog u tüm regular kanalllar için aktif ettik
	// ama sadece bir kanal kullanacağımız için de tek bir kanaldan gelen veriyi izle
	ADC1->CR1 |=  (1 << 9);    // AWDSGL = 1 (Sadece seçilen tek kanalı izle)
	ADC1->CR1 |=  (1 << 23);   // AWDEN = 1 (Regular kanalda Watchdog Enable)

	ADC1->CR1 |= (1<<6); // Eğer bu sınırların dışına çıkılırsa Analog watchdog flag i 1 olur ve interrupt üretilir bu interrupt içerisinde bir uyarı göndereceğim bilgisayara UART ile

	NVIC_EnableIRQ(ADC_IRQn); // AWD bir threshold aşması yakalarsa diye CPU nun bu interrupt isteğini enable et.

	// ADC1 de dönüşüm sırasında sadece bir adet dönüşüm olacak ve sadece kanal 11 üzerinde conversion gerçekleşecek.
	// Bu yüzden önce kaç kanala conversion yapılacağını seç:
	ADC1->SQR1 &= ~(15<<20); // 1 adet kanal var sequence de

	// bu sequence nin 1. sırasında da 11. kanal var
	ADC1->SQR3 &= ~(31<<0);
	ADC1->SQR3 |= (11<<0);

	// DMA kullan:
    /*CONT = 1: ADC'ye "Durmadan sürekli dönüşüm (conversion) yap" der. (Bu sadece ADC'nin kendi iç çalışma modudur).

      DDS = 1: ADC'ye "Aksini söyleyene kadar yaptığın her bir dönüşümün sonunda DMA'ya yeni bir istek (request) sinyali göndermeye devam et" der.

      Eğer DDS = 0 kalsaydı, ADC kendi kendine sürekli ölçüm yapmaya devam ederdi ama ilk transferden sonra DMA'ya sinyal göndermeyi kestiği için DMA yeni verileri RAM'e taşımazdı.
      DDS = 1 yaparak ADC ile DMA arasındaki o köprüyü ve istek akışını sürekli açık tutmuş olduk.*/

	ADC1->CR2 |= (1 << 8) | (1 << 9); // DMA Enable (Bit 8) + DDS (Bit 9) (Çözüm 2)


	// ADC1 modülünü aç
	ADC1->CR2 |= (1<<0);

	// Regular channel lar için conversion u başlat
	ADC1->CR2 |= (1<<30);

}

void NVIC_Config(){
	NVIC_SetPriority(ADC_IRQn, 2); //ADC den gelecek olan interrupt isteteğinin(IRQ) önceliği 2 olsun. Analog watchdog için interrupt olup olmadığını kontrol edeceğim
	NVIC_EnableIRQ(ADC_IRQn); // ADC Interrupt Hattını aktifletirdim

	// HAL için, kesmeye gitsin diye
	SysTick_Config(SystemCoreClock / 1000);
	NVIC_SetPriority(SysTick_IRQn,1);
	NVIC_EnableIRQ(SysTick_IRQn);

	// DMA1 transfer tamamlanınca kesmeye gitsin
	NVIC_SetPriority(DMA1_Stream4_IRQn,3);
	NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

void GPIO_Config(){

	// PortC_1 ADC için, PortA_1 TIM2 için; UART4 için de PortC_10 pinini konfigüre et;

	RCC->AHB1ENR |= (1<<2) | (1<<0); // PortC ve PortA için AHB1 hattını enable et.

	// PortC_1 pini ADC için konfigüre et;

	// GPIOC nin 1. pininin modunu analog mod seç
	GPIOC->MODER &= ~(3<<2); // bitleri temizle
	GPIOC->MODER |= (3<<2); // bitleri 11 olarak ayarla


	// PortA_1 pini TIM2 için konfigüre et;

	// 2. PA1 Pinini Alternate Function (AF1 - TIM2) Moduna Al
	GPIOA->MODER  &= ~(3 << (1 * 2));   // Clean bits
	GPIOA->MODER  |=  (2 << (1 * 2));   // 10: Alternate Function
	GPIOA->AFR[0] &= ~(0xF << (1 * 4)); // Clean AFRL1
	GPIOA->AFR[0] |=  (1 << (1 * 4));   // 0001: AF1 (TIM2)

	// PC10 (UART4_TX) -> Alternate Function 8 (AF8)
	GPIOC->MODER  &= ~(3 << (10 * 2));
	GPIOC->MODER  |=  (2 << (10 * 2));
	GPIOC->AFR[1] &= ~(0xF << ((10 - 8) * 4)); // AFRH (AFR[1]) kullanılır!
	GPIOC->AFR[1] |=  (8 << ((10 - 8) * 4));   // AF8 (UART4)

}

void SystemClockConfigUpdate(){

	// Amaç: SYSCLK=168MHz çalıştırmak

	/* FLASH ayarları */
	FLASH->ACR |= (5<<0); // Bu satır LATENCY ayarını yapıyor, LATENCY = 5 → 5 wait states (STM32F4’de 168 MHz çalıştırmak için datasheet’e göre 5 wait state gerekiyor)
	FLASH->ACR |= (1<<8); // PRFTEN (Prefetch enable) bitini açar.
	FLASH->ACR |= (1<<9); // ICEN (Instruction cache enable) bitini açar.
	FLASH->ACR |= (1<<10); // DCEN (Data cache enable) bitini açar.


	RCC->CR |= (1<<16); // HSE enable edildi kullanılmak üzere
	while((RCC->CR&0x00020000)!=0x00020000); // HSE ready flag i 1 olup HSE nin çalışmaya hazır olduğunu söyleyene kadar bekle
	RCC->CR |= (1<<19); // HSE clock un çalışıp çalışmadığını izleyen controle detector ü enable et

	// PLL ayarları

	// PLL_M = 8
	RCC->PLLCFGR &= ~(0x3F<<0); // öncelikle ilgili bitleri temizledim.
	RCC->PLLCFGR |= (1<<3);
	// PLL_M çıkışı(PLL_N girişi) = 1MHz artık

	// PLL_N=336MHz olmalı
	RCC->PLLCFGR &= ~(0x1FF<<6); // ilgili bitleri temizle
	RCC->PLLCFGR |= (1<<14) | (1<<12) | (1<<10);

	// PLL_P=2
	RCC->PLLCFGR &= ~(3<<16); // Zaten bu hali ile PLL_P=2 olmuş olur

	RCC->PLLCFGR |= (1<<22);   // PLLSRC = HSE

	// PLL kullanacağımızı belirtmek için PLLON enable edilmeli
	RCC->CR |= (1<<24);

	while((RCC->CR & (1<<25)) == 0); // PLLRDY

	// PLLCLK yı SYSCLK kullanacağımı söylüyorum;
	RCC->CFGR &= ~(3<<0); // önce bitleri temizle
	RCC->CFGR |= (1<<1);

	// PLL selected as system clock
	RCC->CFGR &= ~(3<<0);
	RCC->CFGR |= (1<<1);

	while((RCC->CFGR & (3<<2)) != (2<<2)); //Switch’in tamamlandığını kontrol et

	// AHB Prescaler(HPRE biti) = 1 olmalı ki HCLK=168MHz olsun
	RCC->CFGR &= ~(0xF<<4);

	// SysTick frekans=168MHz olsun istediğim için HCLK/8 değil HCLK kaynağını direkt kullanacağım.
	// Bu nedenle STK_CTRL register ında clock source olarak HCLK yı seçmek istediğim için 2. biti "1" olarak ayarla
	SysTick->CTRL |= (1<<2);

	//  APB1=42, APB2=84 MHz de çalışsın diye PPRE1=4, PPRE2=2 olarak ayalarnmalı RCC_CFGR register ında
	RCC->CFGR &= ~(0x3F<<10); // bitleri temizle
	RCC->CFGR |= (5 << 10) | (4 << 13); // APB1=/4, APB2=/2

	SystemCoreClockUpdate(); // Donanımda değiştirdiğimiz SYSCLK frekansını CMSIS tarafındaki SystemCoreClock değişkenine güncelleyerek,
		                         // yazılımın ve kütüphanelerin doğru CPU frekansını kullanmasını sağlamaktır.
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
