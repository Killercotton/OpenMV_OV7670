# cmdline: -v -v
# test printing of all bytecodes

def f():
    # constants
    a = None + False + True
    a = 0
    a = 1000
    a = -1000

    # constructing data
    a = 1
    b = (1, 2)
    c = [1, 2]
    d = {1, 2}
    e = {}
    f = {1:2}
    g = 'a'
    h = b'a'

    # unary/binary ops
    i = 1
    j = 2
    k = a + b
    l = -a
    m = not a
    m = a == b == c
    m = not (a == b and b == c)

    # attributes
    n = b.c
    b.c = n

    # subscript
    p = b[0]
    b[0] = p

    # slice
    a = b[::]

    # sequenc unpacking
    a, b = c

    # tuple swapping
    a, b = b, a
    a, b, c = c, b, a

    # del fast
    del a

    # globals
    global gl
    gl = a
    del gl

    # comprehensions
    a = (b for c in d if e)
    a = [b for c in d if e]
    a = {b:b for c in d if e}

    # function calls
    a()
    a(1)
    a(b=1)
    a(*b)

    # method calls
    a.b()
    a.b(1)
    a.b(c=1)
    a.b(*c)

    # jumps
    if a:
        x
    else:
        y
    while a:
        b
    while not a:
        b

    # for loop
    for a in b:
        c

    # exceptions
    try:
        while a:
            break
    except:
        b
    finally:
        c

    # with
    with a:
        b

    # closed over variables
    x = 1
    def closure():
        a = x + 1
        x = 1
        del x

    # import
    import a
    from a import b
    from a import *

    # raise
    raise
    raise 1

    # return
    return
    return 1

# functions with default args
def f(a=1):
    pass

    def f(b=2):
        return b + a

# function which yields
def f():
    yield
    yield 1
    yield from 1

# class
class Class:
    pass
