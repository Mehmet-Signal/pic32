/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include "definitions.h"                // SYS function prototypes


// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************


// 1. Kesme an?nda çal??acak fonksiyonun (Callback)

unsigned int test;

void BenimTimerKesmem(uint32_t status, uintptr_t context)
{
    // LED'i tersle (Toggle)
    // GPIO_PinToggle fonksiyonunu veya kendi tan?mlad???n makroyu kullan
    test++;
    if (test > 5)
    {
        LED_IO1_Toggle();
        LED_IO2_Toggle();
        test = 0;
    }
}

int main(void)
{
    /* Initialize all modules */
    SYS_Initialize(NULL);
    TMR2_CallbackRegister(BenimTimerKesmem, 0);
    LED_IO1_Set();
    LED_IO1_Clear();
    TMR2_Start();
    while (true)
    {
        /* Maintain state machines of all polled MPLAB Harmony modules. */
        SYS_Tasks();
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE);
}


/*******************************************************************************
 End of File
 */

