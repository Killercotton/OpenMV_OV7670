.. _machine.UART:

class UART -- duplex serial communication bus
=============================================

UART implements the standard UART/USART duplex serial communications protocol.  At
the physical level it consists of 2 lines: RX and TX.  The unit of communication
is a character (not to be confused with a string character) which can be 8 or 9
bits wide.

UART objects can be created and initialised using::

    from machine import UART

    uart = UART(1, 9600)                         # init with given baudrate
    uart.init(9600, bits=8, parity=None, stop=1) # init with given parameters

.. only:: port_machineoard

    Bits can be 7, 8 or 9.  Parity can be None, 0 (even) or 1 (odd).  Stop can be 1 or 2.
    
    *Note:* with parity=None, only 8 and 9 bits are supported.  With parity enabled,
    only 7 and 8 bits are supported.

.. only:: port_wipy

    Bits can be 5, 6, 7, 8.  Parity can be ``None``, ``UART.EVEN`` or ``UART.ODD``.  Stop can be 1 or 2.


A UART object acts like a stream object and reading and writing is done
using the standard stream methods::

    uart.read(10)       # read 10 characters, returns a bytes object
    uart.readall()      # read all available characters
    uart.readline()     # read a line
    uart.readinto(buf)  # read and store into the given buffer
    uart.write('abc')   # write the 3 characters

.. only:: port_machineoard

    Individual characters can be read/written using::

        uart.readchar()     # read 1 character and returns it as an integer
        uart.writechar(42)  # write 1 character

    To check if there is anything to be read, use::

        uart.any()               # returns True if any characters waiting

    *Note:* The stream functions ``read``, ``write``, etc. are new in MicroPython v1.3.4.
    Earlier versions use ``uart.send`` and ``uart.recv``.

.. only:: port_wipy

    To check if there is anything to be read, use::

        uart.any()               # returns the number of characters available for reading

Constructors
------------

.. only:: port_wipy

    .. class:: machine.UART(bus, ...)
    
       Construct a UART object on the given bus.  ``bus`` can be 0 or 1.
       If the bus is not given, the default one will be selected (0) or the selection
       will be made based on the given pins.

Methods
-------

.. only:: port_wipy

    .. method:: uart.init(baudrate=9600, bits=8, parity=None, stop=1, \*, pins=(TX, RX, RTS, CTS))
    
       Initialise the UART bus with the given parameters:
    
         - ``baudrate`` is the clock rate.
         - ``bits`` is the number of bits per character, 7, 8 or 9.
         - ``parity`` is the parity, ``None``, ``UART.EVEN`` or ``UART.ODD``.
         - ``stop`` is the number of stop bits, 1 or 2.
         - ``pins`` is a 4 or 2 item list indicating the TX, RX, RTS and CTS pins (in that order).
           Any of the pins can be None if one wants the UART to operate with limited functionality.
           If the RTS pin is given the the RX pin must be given as well. The same applies to CTS. 
           When no pins are given, then the default set of TX and RX pins is taken, and hardware 
           flow control will be disabled. If pins=None, no pin assignment will be made.

.. method:: uart.deinit()

   Turn off the UART bus.

.. method:: uart.any()

   Return the number of characters available for reading.

.. method:: uart.read([nbytes])

   Read characters.  If ``nbytes`` is specified then read at most that many bytes.

   Return value: a bytes object containing the bytes read in.  Returns ``None``
   on timeout.

.. method:: uart.readall()

   Read as much data as possible.

   Return value: a bytes object or ``None`` on timeout.

.. method:: uart.readinto(buf[, nbytes])

   Read bytes into the ``buf``.  If ``nbytes`` is specified then read at most
   that many bytes.  Otherwise, read at most ``len(buf)`` bytes.

   Return value: number of bytes read and stored into ``buf`` or ``None`` on
   timeout.

.. method:: uart.readline()

   Read a line, ending in a newline character.

   Return value: the line read or ``None`` on timeout.

.. method:: uart.write(buf)

   Write the buffer of bytes to the bus.

   Return value: number of bytes written or ``None`` on timeout.

.. method:: uart.sendbreak()

   Send a break condition on the bus.  This drives the bus low for a duration
   of 13 bits.
   Return value: ``None``.

.. only:: port_wipy

    .. method:: uart.irq(trigger, priority=1, handler=None, wake=machine.IDLE)

       Create a callback to be triggered when data is received on the UART.

           - ``trigger`` can only be ``UART.RX_ANY``
           - ``priority`` level of the interrupt. Can take values in the range 1-7.
             Higher values represent higher priorities.
           - ``handler`` an optional function to be called when new characters arrive.
           - ``wake`` can only be ``machine.IDLE``.

       .. note::

          The handler will be called whenever any of the following two conditions are met:

              - 8 new characters have been received.
              - At least 1 new character is waiting in the Rx buffer and the Rx line has been
                silent for the duration of 1 complete frame.

          This means that when the handler function is called there will be between 1 to 8 
          characters waiting.

       Returns an irq object.

Constants
---------

.. data:: UART.EVEN
.. data:: UART.ODD

    parity types (anlong with ``None``)

.. data:: UART.RX_ANY

    IRQ trigger sources
