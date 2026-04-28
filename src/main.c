/*
 * nrfx SPIM example for nRF54L15 DK
 *
 * Transfers 255 bytes via SPIM21 every 1 second using the nrfx API directly
 * (interrupt-driven, non-blocking). The Zephyr SPI driver is not used.
 *
 * Loopback test: connect P1.13 (MOSI) to P1.12 (MISO) with a jumper wire.
 * Each transfer verifies that received bytes match the sent bytes and
 * reports PASS or FAIL over the serial console.
 *
 * Pins (nRF54L15 DK expansion header):
 *   SCK  = P1.11
 *   MOSI = P1.13  <-- connect to MISO
 *   MISO = P1.12  <-- connect to MOSI
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <nrfx_spim.h>
#include <string.h>

LOG_MODULE_REGISTER(spim_example, LOG_LEVEL_INF);

/* DT node for SPIM21, used to look up IRQ number and priority */
#define SPIM_NODE DT_NODELABEL(spi21)

/*
 * Expansion header pin numbers (NRF_GPIO_PIN_MAP(port, pin) = port*32 + pin).
 * P1.11 = 43, P1.12 = 44, P1.13 = 45
 */
#define SCK_PIN  NRF_GPIO_PIN_MAP(1, 11)  /* expansion header SCK  */
#define MOSI_PIN NRF_GPIO_PIN_MAP(1, 13)  /* expansion header MOSI */
#define MISO_PIN NRF_GPIO_PIN_MAP(1, 12)  /* expansion header MISO */

#define TRANSFER_LEN 255U

/* SPIM21 driver instance */
static nrfx_spim_t spim = NRFX_SPIM_INSTANCE(NRF_SPIM21);

/*
 * TX/RX buffers must be in SRAM for EasyDMA access.
 * Static global allocation satisfies this requirement.
 */
static uint8_t tx_buf[TRANSFER_LEN];
static uint8_t rx_buf[TRANSFER_LEN];

/* Semaphore signalled by the 1-second timer */
static K_SEM_DEFINE(trigger_sem, 0, 1);

/* Semaphore signalled by the SPIM done interrupt */
static K_SEM_DEFINE(done_sem, 0, 1);

/* Running transfer counter */
static uint32_t xfer_count;

/*
 * nrfx SPIM event handler (called from ISR context).
 * Signals the main thread that the transfer has completed.
 */
static void spim_event_handler(nrfx_spim_event_t const *p_event, void *p_context)
{
	ARG_UNUSED(p_context);

	if (p_event->type == NRFX_SPIM_EVENT_DONE) {
		k_sem_give(&done_sem);
	}
}

/*
 * k_timer expiry callback: fires every 1 second from system timer context.
 * Unblocks the main thread to start the next transfer.
 */
static void periodic_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_sem_give(&trigger_sem);
}

K_TIMER_DEFINE(periodic_timer, periodic_timer_handler, NULL);

int main(void)
{
	LOG_INF("nrfx SPIM example starting on nRF54L15 DK");
	LOG_INF("SPIM21  SCK=P1.11  MOSI=P1.13  MISO=P1.12");
	LOG_INF("Loopback test: connect P1.13 (MOSI) <--> P1.12 (MISO)");

	/* Fill TX buffer with incrementing pattern 0x00 .. 0xFE */
	for (uint32_t i = 0; i < TRANSFER_LEN; i++) {
		tx_buf[i] = (uint8_t)i;
	}

	/*
	 * Register the SPIM21 IRQ handler.
	 *
	 * IRQ_CONNECT is a compile-time macro that wires the hardware IRQ to
	 * nrfx_spim_irq_handler with &spim as the context parameter.
	 * irq_enable() unmasks the line at the NVIC level; the peripheral-level
	 * interrupt enable is set by nrfx_spim_init() below.
	 */
	IRQ_CONNECT(DT_IRQN(SPIM_NODE),
		    DT_IRQ(SPIM_NODE, priority),
		    nrfx_spim_irq_handler,
		    &spim,
		    0);
	irq_enable(DT_IRQN(SPIM_NODE));

	/*
	 * Initialise SPIM21.
	 * NRFX_SPIM_DEFAULT_CONFIG sets: mode 0, MSB first, orc=0xFF.
	 * SPIM21 core clock is 16 MHz; 4 MHz => prescaler = 4 (valid).
	 */
	nrfx_spim_config_t config =
		NRFX_SPIM_DEFAULT_CONFIG(SCK_PIN, MOSI_PIN, MISO_PIN,
					 NRF_SPIM_PIN_NOT_CONNECTED);
	config.frequency = NRFX_MHZ_TO_HZ(4U);

	int err = nrfx_spim_init(&spim, &config, spim_event_handler, NULL);
	if (err != 0) {
		LOG_ERR("nrfx_spim_init failed (err 0x%08x)", err);
		return -EIO;
	}

	LOG_INF("SPIM21 initialised at 4 MHz. Sending %u bytes every 1 s ...",
		TRANSFER_LEN);

	/* Start the 1-second periodic timer (first tick after 1 s) */
	k_timer_start(&periodic_timer, K_SECONDS(1), K_SECONDS(1));

	while (true) {
		/* Block until the 1-second tick arrives */
		k_sem_take(&trigger_sem, K_FOREVER);

		/* Preset RX buffer to 0xAA so mismatches are visible */
		memset(rx_buf, 0xAA, sizeof(rx_buf));

		nrfx_spim_xfer_desc_t xfer =
			NRFX_SPIM_XFER_TRX(tx_buf, TRANSFER_LEN,
					    rx_buf, TRANSFER_LEN);

		err = nrfx_spim_xfer(&spim, &xfer, 0);
		if (err != 0) {
			LOG_ERR("nrfx_spim_xfer failed (err 0x%08x)", err);
			continue;
		}

		/* Wait for the SPIM done interrupt (timeout: 100 ms) */
		int ret = k_sem_take(&done_sem, K_MSEC(100));
		if (ret != 0) {
			LOG_ERR("Transfer timed out!");
			continue;
		}

		xfer_count++;

		/* Verify loopback: every RX byte must equal the TX byte */
		bool pass = true;

		for (uint32_t i = 0; i < TRANSFER_LEN; i++) {
			if (rx_buf[i] != tx_buf[i]) {
				LOG_ERR("#%u FAIL at byte %u: TX=0x%02x RX=0x%02x",
					xfer_count, i, tx_buf[i], rx_buf[i]);
				pass = false;
				break;
			}
		}

		if (pass) {
			LOG_INF("#%u PASS: %u bytes OK", xfer_count, TRANSFER_LEN);
		}
	}

	return 0;
}
