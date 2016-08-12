:mod:`ustruct` -- pack and unpack primitive data types
======================================================

.. module:: ustruct
   :synopsis: pack and unpack primitive data types

See `Python struct <https://docs.python.org/3/library/struct.html>`_ for more
information.

Functions
---------

.. function:: calcsize(fmt)

   Return the number of bytes needed to store the given `fmt`.

.. function:: pack(fmt, v1, v2, ...)

   Pack the values `v1`, `v2`, ... according to the format string `fmt`.
   The return value is a bytes object encoding the values.

.. function:: pack_into(fmt, buffer, offset, v1, v2, ...)

   Pack the values `v1`, `v2`, ... according to the format string `fmt`
   into a `buffer` starting at `offset`. `offset` may be negative to count
   from the end of `buffer`.

.. function:: unpack(fmt, data)

   Unpack from the `data` according to the format string `fmt`.
   The return value is a tuple of the unpacked values.

.. function:: unpack_from(fmt, data, offset=0)

   Unpack from the `data` starting at `offset` according to the format string
   `fmt`. `offset` may be negative to count from the end of `buffer`. The return
   value is a tuple of the unpacked values.
