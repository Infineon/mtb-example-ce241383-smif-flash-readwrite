/*******************************************************************************
* File Name: main.c
*
* Description: This is the source code for the Serial Memory InterFace
* Flash Read and Write example
*
* Related Document: See README.md
*
*
********************************************************************************
 * (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
*******************************************************************************/
#include "mtb_hal.h"
#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_retarget_io.h"
#include "cycfg_qspi_memslot.h"
#include "mtb_serial_memory.h"
#include <inttypes.h>

/*******************************************************************************
* Macros
*******************************************************************************/
/* Memory Read/Write size */
#define PACKET_SIZE             (64u)

/* Used when an array of data is printed on the console */
#define LED_TOGGLE_DELAY_MSEC   (1000u)

/* COnfigured Chip select */
#define CHIP_SELECT MTB_SERIAL_MEMORY_CHIP_SELECT_0

/* A timeout in microseconds for blocking APIs in use */
#define SMIF_INIT_TIMEOUT       (10000UL)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* For the Retarget -IO (Debug UART) usage */
static cy_stc_scb_uart_context_t    CYBSP_DEBUG_UART_context;
static mtb_hal_uart_t               CYBSP_DEBUG_UART_hal_obj;

/* For SMIF usage */
cy_stc_smif_context_t smifContext;
mtb_serial_memory_t serial_memory_obj;
uint32_t start_address = 0;
uint32_t xip_start_address = 0x60000000;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/


/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: check_status
********************************************************************************
* Summary:
*  Prints the message, indicates the non-zero status by turning the LED on, and
*  asserts the non-zero status.
*
* Parameters:
*  message - message to print if status is non-zero.
*  status - status for evaluation.
*
* Return:
*  void
*
*******************************************************************************/
void check_status(char *message, uint32_t status)
{
    if (0u != status)
    {
        printf("\r\n=====================================================");
        printf("\r\nFAIL: %s", message);
        printf("\r\nError Code: 0x%08" PRIx32, status);
        printf("\r\n=====================================================\r\n");

        /* On failure, turn the LED ON */
        Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM, CYBSP_LED_STATE_ON);
        while(true); /* Wait forever here when error occurs. */
    }
}

/*******************************************************************************
* Function Name: print_array
********************************************************************************
* Summary:
*  Prints the content of the buffer to the UART console.
*
* Parameters:
*  message - message to print before array output
*  buf - buffer to print on the console.
*  size - size of the buffer.
*
* Return:
*  void
*
*******************************************************************************/
void print_array(char *message, uint8_t *buf, uint32_t size)
{
    printf("\r\n%s (%"PRIu32" bytes):\r\n", message, size);
    printf("-------------------------\r\n");

    for (uint32_t index = 0; index < size; index++)
    {
        printf("0x%02X ", buf[index]);

        if (0u == ((index + 1) % 16u))
        {
            printf("\r\n");
        }
    }
}


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  This is the main function for CM4 CPU. It does...
*     1. Initializes UART for console output and SMIF for interfacing a QSPI
*       flash.
*     2. Performs erase followed by write and verifies the written data by
*       reading it back.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    cy_en_smif_status_t result_smif;
    uint8_t tx_buf[PACKET_SIZE];
    uint8_t rx_buf[PACKET_SIZE];
    uint32_t sectorSize = PACKET_SIZE;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    /* Board initialization failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

        /* Initialize retarget-io to use the debug UART port */
    /* Debug UART init */
    result = (cy_rslt_t)Cy_SCB_UART_Init(CYBSP_DEBUG_UART_HW, &CYBSP_DEBUG_UART_config, &CYBSP_DEBUG_UART_context);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    Cy_SCB_UART_Enable(CYBSP_DEBUG_UART_HW);

    /* Setup the HAL UART */
    result = mtb_hal_uart_setup(&CYBSP_DEBUG_UART_hal_obj, &CYBSP_DEBUG_UART_hal_config, &CYBSP_DEBUG_UART_context, NULL);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    result = cy_retarget_io_init(&CYBSP_DEBUG_UART_hal_obj);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("****************** Serial Flash Read and Write ****************** \r\n\n");

    /* Initialize the SMIF as a communication */
    result_smif = Cy_SMIF_Init(SMIF0_HW, &SMIF0_config, SMIF_INIT_TIMEOUT, &smifContext);
    if (result_smif != CY_SMIF_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enables the operation of the SMIF */
    Cy_SMIF_Enable(SMIF0_HW, &smifContext);

    result = mtb_serial_memory_setup(&serial_memory_obj, CHIP_SELECT, SMIF0_HW, SMIF0_hal_config.clock, &smifContext, &smifBlockConfig);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    printf("Current active chip is %" PRIu32 "\r\n", mtb_serial_memory_get_active_chip(&serial_memory_obj));
    printf("Total chips configured is %" PRIu32 "\r\n", mtb_serial_memory_get_chip_count(&serial_memory_obj));
    printf("Total Memory Size is %" PRIu32 " bytes\r\n", (uint32_t)mtb_serial_memory_get_size(&serial_memory_obj));

    /* Use last sector to erase for flash operation */
    sectorSize = mtb_serial_memory_get_erase_size(&serial_memory_obj, start_address);

    /* Erase before write */
    printf("\r\n1. Erasing %" PRIu32 " bytes of memory\r\n", sectorSize);
    result = mtb_serial_memory_erase(&serial_memory_obj, start_address, sectorSize);
    check_status("Erasing memory failed", result);

    /* Read after Erase to confirm that all data is 0xFF */
    memset(rx_buf, 0x00u, PACKET_SIZE);
    printf("\r\n2. Reading after Erase & verifying that each byte is 0xFF\r\n");
    result = mtb_serial_memory_read(&serial_memory_obj, start_address, PACKET_SIZE, rx_buf);
    check_status("Reading memory failed", result);

    print_array("Received Data", rx_buf, PACKET_SIZE);
    memset(tx_buf, 0xFFu, PACKET_SIZE);
    check_status("Flash contains data other than 0xFF after erase", memcmp(tx_buf, rx_buf, PACKET_SIZE));

    /* Prepare the TX buffer */
    for (uint32_t index = 0; index < PACKET_SIZE; index++)
    {
        tx_buf[index] = (uint8_t)index;
    }

    /* Write the content of the TX buffer to the memory */
    printf("\r\n3. Writing data to memory\r\n");
    result = mtb_serial_memory_write(&serial_memory_obj, start_address, PACKET_SIZE, tx_buf);
    check_status("Writing to memory failed", result);

    print_array("Written Data", tx_buf, PACKET_SIZE);

    /* Read back after Write for verification */
    printf("\r\n4. Reading back for verification\r\n");
    result = mtb_serial_memory_read(&serial_memory_obj, start_address, PACKET_SIZE, rx_buf);
    check_status("Reading memory failed", result);
    print_array("Received Data", rx_buf, PACKET_SIZE);

    /* Check if the transmitted and received arrays are equal */
    check_status("Read data does not match with written data. Read/Write "
            "operation failed.", memcmp(tx_buf, rx_buf, PACKET_SIZE));

    /* Enable XIP mode */
    result = mtb_serial_memory_enable_xip(&serial_memory_obj, true);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Clear Rx buffer */
    memset(rx_buf, 0x00u, PACKET_SIZE);


    /* Read back in XIP mode after Write for verification */
    uint8_t *location_ptr = (uint8_t *)xip_start_address;
    printf("\r\n5. Reading back in XIP mode for verification\r\n");

    for (uint32_t index = 0; index < PACKET_SIZE;  index++)
    {
        rx_buf[index] = *location_ptr;
        location_ptr++;
    }

    print_array("Received Data", rx_buf, PACKET_SIZE);

    /* Check if the transmitted and received arrays are equal */
    check_status("Read data does not match with written data. Read/Write "
            "operation failed.", memcmp(tx_buf, rx_buf, PACKET_SIZE));

    printf("\r\n=========================================================\r\n");
    printf("SUCCESS: Read data matches with written data!\r\n");
    printf("=========================================================\r\n");

    for (;;)
    {
        Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM);
        Cy_SysLib_Delay(LED_TOGGLE_DELAY_MSEC);
    }
}
/* [] END OF FILE */
