/**************************************************************************************************
  Filename:       uart_intfc.c
  Revised:        $Date: 2009-08-17 10:50:58 -0700 (Mon, 17 Aug 2009) $
  Author:         $Author: jnoxon $

  Description:    This file supports the SimpliciTI-compatible UART API functions.

    Code leveraged from Texas Instruments, Inc.
	Modified by:	Niket Shah
					Maulik Kapuria
					Vishal Verma
**************************************************************************************************/

/******************************************************************************
 * INCLUDES
 */
#include "uart_intfc.h"
#include <stdlib.h>

#include "uart.h"

/******************************************************************************
 * CONSTANTS AND DEFINES
 */
#ifndef RX_TX_BUFFER_SIZE       /* this value must be at least 2. */
#define RX_TX_BUFFER_SIZE 50
#endif

/******************************************************************************
 * MACROS
 */
#define RX_TX_BUFFER_THROTTLE_LIMIT ( ( ( UART_BAUD_RATE ) > 15000 \
                                       && ( RX_TX_BUFFER_SIZE ) > 8 ) \
                                             ? ( ( RX_TX_BUFFER_SIZE ) - 4 )\
                                             : ( ( RX_TX_BUFFER_SIZE ) - 1 ) )

/******************************************************************************
 * LOCAL VARIABLES
 */
static unsigned char rx_buff[RX_TX_BUFFER_SIZE];
static unsigned char tx1_buff[RX_TX_BUFFER_SIZE];
static unsigned char tx0_buff[RX_TX_BUFFER_SIZE];
static unsigned char* rx_head = rx_buff;
static unsigned char* rx_tail = rx_buff;
static unsigned char* tx1_head = tx1_buff;
static unsigned char* tx1_tail = tx1_buff;
static unsigned char* tx0_head = tx0_buff;
static unsigned char* tx0_tail = tx0_buff;

/******************************************************************************
 * LOCAL FUNCTIONS
 */

/* uart data handler function prototypes */
bool tx1_handler( unsigned char* c );
bool rx_handler( unsigned char c );
bool tx0_handler( unsigned char* c );

int buffer_free_space( unsigned char* head, unsigned char* tail );
int buffer_used_space( unsigned char* head, unsigned char* tail );
bool push_buffer( unsigned char** head, unsigned char* tail,
                  unsigned char* buff, unsigned char* data, int len );
int pop_buffer( unsigned char* head, unsigned char** tail,
                unsigned char* buff, unsigned char* data, int max_len );

/******************************************************************************
 * GLOBAL FUNCTIONS
 */

/******************************************************************************
 * @fn          uart_intfc_init
 *
 * @brief       Initialize UART interface. Buffer pointers are initialized.
 *
 * input parameters
 *
 * output parameters
 *
 * @return   void
 */
void uart_intfc_init( void )
  {
  /* initialize the buffer pointers in case we are re-initialized */
  rx_head = rx_buff;
  rx_tail = rx_buff;
  tx1_head = tx1_buff;
  tx1_tail = tx1_buff;
  tx0_head = tx0_buff;
  tx0_tail = tx0_buff;

  uart_init( ); /* initialize the uart for operations */
  uart_rx_message( rx_handler ); /* enable us to receive uart data */

#if( defined UART_HARDWARE_HANDSHAKE_IN_SOFTWARE )
   UART_ASSERT_RTS( UART_RTS_DEASSERTED ); /* release flow control */
#endif

  return;
  }

/******************************************************************************
 * @fn          buffer_free_space
 *
 * @brief       Calculates amount of freespace in buffer from <head> and <tail>
 *              addresses
 *
 * input parameters
 * @param   head       - pointer to buffer head
 * @param   tail       - pointer to buffer tail
 *
 * output parameters
 *
 * @return   Buffer free space count in bytes
 */
int buffer_free_space( unsigned char* head, unsigned char* tail )
  {
  /* the free count is the buffer size minus the used count minus one
   * because we don't want the pointers to ever get back on top of each other
   * because that would indicate an empty buffer. */
  return RX_TX_BUFFER_SIZE - buffer_used_space( head, tail ) - 1;
  }

/******************************************************************************
 * @fn          buffer_used_space
 *
 * @brief       Calculates amount of used space in buffer from <head> and <tail>
 *              addresses
 *
 * input parameters
 * @param   head       - pointer to buffer head
 * @param   tail       - pointer to buffer tail
 *
 * output parameters
 *
 * @return   Buffer used space count in bytes
 */
int buffer_used_space( unsigned char* head, unsigned char* tail )
  {
  ptrdiff_t used;

  used = head - tail; /* get used count */
  if( used < 0 ) /* if the pointers were wrapped */
    used += RX_TX_BUFFER_SIZE; /* correct the count */

  return used; /* return used count */
  }

/******************************************************************************
 * @fn          push_buffer
 *
 * @brief       Pushes bytes of data onto the specified buffer. Assumes on
 *              entry that <data>, <buff>, <tail>, and <head> are all valid
 *              pointers
 *
 * input parameters
 * @param   tail       - pointer to buffer tail
 * @param   buff       - pointer to buffer (push target)
 * @param   data       - pointer to data to be pushed into buffer
 * @param   len        - length in bytes of data to be pushed
 *
 * output parameters
 * @param   head       - updated buffer head pointer
 *
 * @return   status of operation.
 *             true          Data successfully pushed into buffer
 *             false         Data not pushed into buffer
 */

bool push_buffer( unsigned char** head, unsigned char* tail,
                  unsigned char* buff, unsigned char* data, int len )
  {
  unsigned char* local_head;
  unsigned char* local_tail = tail;

  BSP_CRITICAL_STATEMENT( local_head = *head );

  /* if no room in the buffer */
  if( buffer_free_space( local_head, local_tail ) < len )
    return false; /* indicate failure to enqueue message */

  /* there is room for the data, put in the buffer */

  do /* put the data in the buffer */
    {
    if( local_head == buff + RX_TX_BUFFER_SIZE ) /* if wrapping around */
      local_head = buff; /* reset pointer */

    *local_head++ = *data++; /* copy over this byte of data */
    } while( --len > 0 ); /* copy all the data to the buffer */

  BSP_CRITICAL_STATEMENT( *head = local_head ); /* update reference value */

  return true;
  }

/******************************************************************************
 * @fn          pop_buffer
 *
 * @brief       Pops the specified number of bytes off of the specified buffer.
 *              Assumes on entry that <data>, <buff>, <tail>, and <head> are
 *              all valid pointers.
 *
 * input parameters
 * @param   head       - pointer to buffer head
 * @param   buff       - pointer to buffer (pop source)
 * @param   data       - pointer to location to store popped data
 * @param   len        - amount of bytes to be popped from buffer
 *
 * output parameters
 * @param   tail       - updated buffer tail pointer
 *
 * @return   number of bytes popped from buffer
 */

int pop_buffer( unsigned char* head, unsigned char** tail,
                unsigned char* buff, unsigned char* data, int max_len )
  {
  unsigned char* local_tail;
  unsigned char* local_head = head;
  int cnt = 0;

  BSP_CRITICAL_STATEMENT( local_tail = *tail );

  /* if the buffer is empty or no data requested */
  if( local_tail == local_head || max_len <= 0 )
    return 0; /* indicate so */

  do /* retrieve the data from the buffer */
    {
    if( local_tail == buff + RX_TX_BUFFER_SIZE ) /* if wrapping around */
      local_tail = buff; /* reset pointer */

    *data++ = *local_tail++; /* copy data from buffer */

    /* while the user needs more data and there is data left in the fifo */
    } while( ++cnt < max_len && local_tail != local_head );

  BSP_CRITICAL_STATEMENT( *tail = local_tail ); /* update reference value */

  return cnt; /* return number of characters retrieved from the buffer */
  }

/******************************************************************************
 * @fn          tx1_send_wait
 *
 * @brief       Enqueue's the message bointed to by <data> which is of length
 *              <len> and initiates its transfer across the UART.  This is a
 *              blocking function in that if the transmit fifo doesn't have
 *              enough room to enqueue the data in its entirety it will push
 *              the data out a piece at a time as the room in the FIFO becomes
 *              available.  The function returns true upon completion of moving
 *              all the data into the FIFO and false if either a NULL pointer
 *              or a length of zero was passed.
 *
 * input parameters
 * @param   data       - pointer to data to be sent
 * @param   len        - length in bytes of data to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Data successfully pushed into transmit buffer
 *             false         Invalid length or NULL data pointer
 */

bool tx1_send_wait( const void* data, size_t len )
  {
  if( len > 0 && data != NULL ) /* if the information looks viable */
    {
    while( len > 0 )            /* while there is data left to transfer */
      {
      int sz = tx1_peek( );      /* get free space in the fifo */

      if( sz > 0 )              /* if there is room for at least some data */
        {
        if( sz > len )          /* if more room than required          */
          sz = len;             /* limit size to just the data to send */

        tx1_send( data, sz );    /* send this portion of the information */

        data = (unsigned char*)data + sz; /* move the pointer */

        len -= sz;              /* adjust the count of remaining data to send */
        }
      }

    return true; /* indicate success */
    }

  return false; /* otherwise indicate failure */
  }

/******************************************************************************
 * @fn          tx1_send
 *
 * @brief       Enqueue's the message pointed to by <data> which is of length
 *              <len> and initiates its transfer across the uart.  true is
 *              returned if there was space in the FIFO to send the data, false
 *              if the FIFO didn't have enough free space to enqueue the data.
 *
 * input parameters
 * @param   data       - pointer to data to be sent
 * @param   len        - length in bytes of data to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Data successfully pushed into transmit buffer
 *             false         Data not successfully pushed into transmit buffer
 */

bool tx1_send( const void* data, size_t len )
{
  bool status;
  unsigned char* tail;

  /* get current state of tail pointer */
  BSP_CRITICAL_STATEMENT( tail = tx1_tail );

  /* put data into transmit buffer */
  status = push_buffer( &tx1_head, tail, tx1_buff, (unsigned char*)data, len );

  if( status != false ) /* if data was put in the buffer properly */
    uart1_tx_message( tx1_handler ); /* notify the irq that data is ready to send */

  return status; /* return status */
  }

/******************************************************************************
 * @fn          tx1_peek
 *
 * @brief       Returns the number of bytes of free space in the transmit FIFO.
 *
 * input parameters
 *
 * output parameters
 *
 * @return   Number of bytes of free space in the transmit FIFO
 */

int tx1_peek( void )
  {
  unsigned char* head;
  unsigned char* tail;

  BSP_CRITICAL_STATEMENT( head = tx1_head; tail = tx1_tail );

  return buffer_free_space( head, tail );
  }

/******************************************************************************
 * @fn          rx_peek
 *
 * @brief       Returns the number of bytes currently available to be read from
 *              the receive FIFO.
 *
 * input parameters
 *
 * output parameters
 *
 * @return   Number of bytes of available in the receive FIFO
 */

int rx_peek( void )
  {
  unsigned char* head;
  unsigned char* tail;

  BSP_CRITICAL_STATEMENT( head = rx_head; tail = rx_tail );

  return buffer_used_space( head, tail );
  }

/******************************************************************************
 * @fn          rx_receive
 *
 * @brief       Fills in the buffer <data> with data from the receive FIFO until
 *              either <max_len> bytes have been transferred into <data> or the
 *              receive queue is emptied.  The actual number of bytes put into
 *              <data> is returned.
 *
 * input parameters
 * @param   data           - pointer to data to be sent
 * @param   max_len        - length in bytes of data to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Data successfully pushed into transmit buffer
 *             false         Data not successfully pushed into transmit buffer
 */

int rx_receive( void* data, int max_len )
  {
  int cnt;
  unsigned char* head;

  /* get current state of head pointer */
  BSP_CRITICAL_STATEMENT( head = rx_head );

  /* retrieve data from buffer */
  cnt = pop_buffer( head, &rx_tail, rx_buff, data, max_len );

#if( defined UART_HARDWARE_HANDSHAKE_IN_SOFTWARE )
  /* if we need to hold off the remote transmitter */
  if( rx_peek( ) < RX_TX_BUFFER_THROTTLE_LIMIT )
     UART_ASSERT_RTS( UART_RTS_DEASSERTED ); /* deassert the RTS line */
#endif

  return cnt; /* indicate the number of bytes retrieved from the buffer */
  }

/******************************************************************************
 * @fn          uart_busy
 *
 * @brief       Returns true if there are characters in the receive FIFO or
 *              transmit buffer and returns false if both FIFOs are empty.
 *
 * input parameters
 *
 * output parameters
 *
 * @return   UART status
 *             true          Data in receive and/or transmit FIFO
 *             false         Both receive and transmit FIFOS empty
 */

bool uart_busy( void )
  {
  int cnt;
  unsigned char* head;
  unsigned char* tail;

  /* get receive buffer count */
  BSP_CRITICAL_STATEMENT( head = rx_head; tail = rx_tail );
  cnt = buffer_used_space( head, tail );

  /* get transmit buffer count */
  BSP_CRITICAL_STATEMENT( head = tx1_head; tail = tx1_tail );
  cnt += buffer_used_space( head, tail );

  return ( ( cnt == 0 ) ? false : true ); /* return status of uart */
  }

/******************************************************************************
 * @fn          tx1_handler
 *
 * @brief       Called by UART transmit interrupt service routine. Pops a byte
 *              off of the transmit FIFO and stores in <c>. The UART
 *              interrupt service routine will transmit the byte in <c> after
 *              calling this function. Returns true if there is still data left
 *              in the transmit FIFO and false if the FIFO has been emptied.
 *
 * input parameters
 * @param   c              - pointer to store byte to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Byte in <c> is NOT the last byte in the buffer
 *             false         Byte in <c> IS the last byte in the buffer
 */

bool tx1_handler( unsigned char* c )
  {
  bool status;
  unsigned char* head;

  /* get current state of head pointer */
  BSP_CRITICAL_STATEMENT( head = tx1_head );

  /* get data off of the transmit buffer */
  pop_buffer( head, &tx1_tail, tx1_buff, c, 1 );

  /* check status of buffer */
  BSP_CRITICAL_STATEMENT( status = tx1_head != tx1_tail );

  return status; /* indicate if this is the last byte in the buffer */
  }

/******************************************************************************
 * @fn          rx_handler
 *
 * @brief       Called by UART receive interrupt service routine. Pushes byte
 *              <c> into the receive FIFO <c>.
 *
 * input parameters
 * @param   c              - byte of data to be pushed onto receive FIFO
 *
 * output parameters
 *
 * @return   Always returns true
 */

bool rx_handler( unsigned char c )
  {
  unsigned char* tail;

  /* get current state of tail pointer */
  BSP_CRITICAL_STATEMENT( tail = rx_tail );

  /* put data onto the receive buffer */
  push_buffer( &rx_head, tail, rx_buff, &c, 1 );

#if( defined UART_HARDWARE_HANDSHAKE_IN_SOFTWARE )
  /* if we need to hold off the remote transmitter */
  if( rx_peek( ) >= RX_TX_BUFFER_THROTTLE_LIMIT )
    UART_ASSERT_RTS( UART_RTS_ASSERTED ); /* assert the RTS line */
#endif

  return true; /* always accept data received from the uart */
  }


/******************************************************************************
 * @fn          tx_send
 *
 * @brief       Enqueue's the message pointed to by <data> which is of length
 *              <len> and initiates its transfer across the uart.  true is
 *              returned if there was space in the FIFO to send the data, false
 *              if the FIFO didn't have enough free space to enqueue the data.
 *
 * input parameters
 * @param   data       - pointer to data to be sent
 * @param   len        - length in bytes of data to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Data successfully pushed into transmit buffer
 *             false         Data not successfully pushed into transmit buffer
 */

bool tx0_send( const void* data, size_t len )
  {
  bool status;
  unsigned char* tail;

  /* get current state of tail pointer */
  BSP_CRITICAL_STATEMENT( tail = tx0_tail );

  /* put data into transmit buffer */
  status = push_buffer( &tx0_head, tail, tx0_buff, (unsigned char*)data, len );

  if( status != false ) /* if data was put in the buffer properly */
    uart0_tx_message( tx0_handler ); /* notify the irq that data is ready to send */

  return status; /* return status */
  }


/******************************************************************************
 * @fn          tx_send_wait
 *
 * @brief       Enqueue's the message bointed to by <data> which is of length
 *              <len> and initiates its transfer across the UART.  This is a
 *              blocking function in that if the transmit fifo doesn't have
 *              enough room to enqueue the data in its entirety it will push
 *              the data out a piece at a time as the room in the FIFO becomes
 *              available.  The function returns true upon completion of moving
 *              all the data into the FIFO and false if either a NULL pointer
 *              or a length of zero was passed.
 *
 * input parameters
 * @param   data       - pointer to data to be sent
 * @param   len        - length in bytes of data to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Data successfully pushed into transmit buffer
 *             false         Invalid length or NULL data pointer
 */

bool tx0_send_wait( const void* data, size_t len )
  {
  if( len > 0 && data != NULL ) /* if the information looks viable */
    {
    while( len > 0 )            /* while there is data left to transfer */
      {
      int sz = tx0_peek( );      /* get free space in the fifo */

      if( sz > 0 )              /* if there is room for at least some data */
        {
        if( sz > len )          /* if more room than required          */
          sz = len;             /* limit size to just the data to send */

        tx0_send( data, sz );    /* send this portion of the information */

        data = (unsigned char*)data + sz; /* move the pointer */

        len -= sz;              /* adjust the count of remaining data to send */
        }
      }

    return true; /* indicate success */
    }

  return false; /* otherwise indicate failure */
  }


/******************************************************************************
 * @fn          tx_peek
 *
 * @brief       Returns the number of bytes of free space in the transmit FIFO.
 *
 * input parameters
 *
 * output parameters
 *
 * @return   Number of bytes of free space in the transmit FIFO
 */

int tx0_peek( void )
  {
  unsigned char* head;
  unsigned char* tail;

  BSP_CRITICAL_STATEMENT( head = tx0_head; tail = tx0_tail );

  return buffer_free_space( head, tail );
  }

/******************************************************************************
 * @fn          tx_handler
 *
 * @brief       Called by UART transmit interrupt service routine. Pops a byte
 *              off of the transmit FIFO and stores in <c>. The UART
 *              interrupt service routine will transmit the byte in <c> after
 *              calling this function. Returns true if there is still data left
 *              in the transmit FIFO and false if the FIFO has been emptied.
 *
 * input parameters
 * @param   c              - pointer to store byte to be sent
 *
 * output parameters
 *
 * @return   status of operation.
 *             true          Byte in <c> is NOT the last byte in the buffer
 *             false         Byte in <c> IS the last byte in the buffer
 */

bool tx0_handler( unsigned char* c )
  {
  bool status;
  unsigned char* head;

  /* get current state of head pointer */
  BSP_CRITICAL_STATEMENT( head = tx0_head );

  /* get data off of the transmit buffer */
  pop_buffer( head, &tx0_tail, tx0_buff, c, 1 );

  /* check status of buffer */
  BSP_CRITICAL_STATEMENT( status = tx0_head != tx0_tail );

  return status; /* indicate if this is the last byte in the buffer */
  }