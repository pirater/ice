// **********************************************************************
//
// Copyright (c) 2001
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#ifndef ICE_UTIL_SHARED_H
#define ICE_UTIL_SHARED_H

#include <IceUtil/Config.h>
//
// This is only needed for the vanilla version of Shared.
//
#include <IceUtil/Mutex.h>

#ifndef WIN32
//
// Linux only. Unfortunately, asm/atomic.h builds non-SMP safe code
// with non-SMP kernels. This means that executables compiled with a
// non-SMP kernel would fail randomly due to concurrency errors with
// reference counting on SMP hosts. Therefore the relevent pieces of
// atomic.h are more-or-less duplicated.
//

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 * 
 * Atomically sets the value of @v to @i.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */ 
#define atomic_set(v,i)		(((v)->counter) = (i))

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 * 
 * Atomically increments @v by 1.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */ 
static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__(
		"lock ; incl %0"
		:"=m" (v->counter)
		:"m" (v->counter));
}

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 * 
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */ 
static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	unsigned char c;

	__asm__ __volatile__(
		"lock ; decl %0; sete %1"
		:"=m" (v->counter), "=qm" (c)
		:"m" (v->counter) : "memory");
	return c != 0;
}

/**
 * atomic_exchange_add - same as InterlockedExchangeAdd. This didn't
 * come from atomic.h (the code was taken from
 * /usr/include/asm/rwsem.h.
 **/
static __inline__ int atomic_exchange_add(int i, atomic_t* v)
{
    int tmp = i;
    __asm__ __volatile__(
	"lock ; xadd %0,(%2)"
	:"+r"(tmp), "=m"(v->counter)
	:"r"(v), "m"(v->counter)
	: "memory");
    return tmp + i;
}
#endif

//
// Base classes for reference counted types. The IceUtil::Handle
// template can be used for smart pointers to types derived from these
// bases.
//
// IceUtil::SimpleShared
// =====================
//
// A non thread-safe base class for reference-counted types.
//
// IceUtil::Shared
// ===============
//
// A thread-safe base class for reference-counted types.
//
namespace IceUtil
{

class SimpleShared : public noncopyable
{
public:

    SimpleShared() :
	_ref(0),
	_noDelete(false)
    {
    }

    virtual ~SimpleShared()
    {
    }

    void __incRef()
    {
	assert(_ref >= 0);
	++_ref;
    }

    void __decRef()
    {
	assert(_ref > 0);
	if (--_ref == 0)
	{
	    if(!_noDelete)
	    {
		delete this;
	    }
	}
    }

    int __getRef()
    {
	return _ref;
    }

    void __setNoDelete(bool b)
    {
	_noDelete = b;
    }

private:

    int _ref;
    bool _noDelete;
};

class Shared : public noncopyable
{
public:

    Shared();
    virtual ~Shared();
    void __incRef();
    void __decRef();
    int __getRef();
    void __setNoDelete(bool);

private:

#ifdef WIN32
    LONG _ref;
#else
    atomic_t _ref;
#endif
    bool _noDelete;

    //
    // For the plain vanilla version.
    //
    // bool _noDelete;
    // int _ref;
    // Mutex _mutex;
    //
};

#ifdef WIN32

inline
Shared::Shared() :
    _ref(0),
    _noDelete(false)
{
}

inline
Shared::~Shared()
{
}

inline void
Shared::__incRef()
{
    assert(InterlockedExchangeAdd(&_ref, 0) >= 0);
    InterlockedIncrement(&_ref);
}

inline void
Shared::__decRef()
{
    assert(InterlockedExchangeAdd(&_ref, 0) > 0);
    if (InterlockedDecrement(&_ref) == 0 && !_noDelete)
    {
	delete this;
    }
}

inline int
Shared::__getRef()
{
    return InterlockedExchangeAdd(&_ref, 0);
}

inline void
Shared::__setNoDelete(bool b)
{
    _noDelete = b;
}

#else

inline
Shared::Shared() :
    _noDelete(false)
{
    atomic_set(&_ref, 0);
}

inline
Shared::~Shared()
{
}

inline void
Shared::__incRef()
{
    assert(atomic_exchange_add(0, &_ref) >= 0);
    atomic_inc(&_ref);
}

inline void
Shared::__decRef()
{
    assert(atomic_exchange_add(0, &_ref) > 0);
    if (atomic_dec_and_test(&_ref) && !_noDelete)
    {
	delete this;
    }
}

inline int
Shared::__getRef()
{
    return atomic_exchange_add(0, &_ref);
}

inline void
Shared::__setNoDelete(bool b)
{
    _noDelete = b;
}
#endif

/*
 * This is the mutex protected version
 *
inline
Shared::Shared() :
    _ref(0),
    _noDelete(false)
{
}

inline
Shared::~Shared()
{
}

inline void
Shared::__incRef();
{
    _mutex.lock();
    assert(_ref >= 0);
    ++_ref;
    _mutex.unlock();
}

inline void
Shared::__decRef()
{
    _mutex.lock();
    bool doDelete = false;
    assert(_ref > 0);
    if (--_ref == 0)
    {
	doDelete = !_noDelete;
    }
    _mutex.unlock();
    if (doDelete)
    {
	delete this;
    }
}

inline int
Shared::__getRef()
{
    _mutex.lock();
    int ref = _ref;
    _mutex.unlock();
    return ref;
}

inline void
Shared::__setNoDelete(bool b)
{
    _mutex.lock();
    _noDelete = b;
    _mutex.unlock();
}
*/

}

#endif
