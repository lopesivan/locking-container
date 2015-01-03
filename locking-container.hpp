/* Container with automatic unlocking, concurrent reads, and deadlock prevention
 * (c) 2014-2015, Kevin P. Barry [ta0kira@gmail.com]
 *
 *
 * This file provides a template class that protects access to an object with
 * locks of various types. Conventional mutex-protection methods are susceptible
 * to bugs caused by forgetting (or neglecting) to lock/unlock the mutex. This
 * class eliminates the need to remember because the only access to the
 * protected object is via a proxy object that holds the lock until the
 * reference count of the proxy reaches 0 (like a shared pointer).
 *
 * This header contains one main class 'locking_container <class, class>', where
 * the first argument is the type of object being protected, and the second is
 * the type of lock to be used. A few lock choices are provided:
 *
 *   - 'rw_lock': This lock allows multiple readers at a time. This is the
 *     default lock used. A write lock can only be obtained if no other readers
 *     or writers have a lock. If a thread attempts to obtain a write lock and
 *     there are readers, it will block until all readers leave, blocking out
 *     all new readers and writers in the meantime.
 *
 *   - 'r_lock': This lock allows multiple readers, but it never allows writers.
 *     This might be useful if you have a container that will never be written
 *     to but you nevertheless need to retain the same container semantics.
 *
 *   - 'w_lock': This lock doesn't make a distinction between readers and
 *     writers; only one thread can hold a lock at any given time. This should
 *     operate faster if you don't need read locks. Note that, for the purposes
 *     of deadlock prevention, this treats all locks as write locks.
 *
 *   - 'broken_lock': This is mostly a joke; however, you can use it to test
 *     pathological cases. This lock will always fail to lock and unlock.
 *
 * Each lock type has a corresponding 'lock_auth' specialization for use with
 * deadlock prevention. All of them have (nearly) identical behavior to their
 * corresponding lock types, as far as how many read and write locks can be held
 * at a given time.
 *
 *   - 'lock_auth <rw_lock>': This auth. type allows the caller to hold multiple
 *     read locks, or a single write lock, but not both. Note that if another
 *     thread is waiting for a write lock on the container and the caller
 *     already has a read lock then the lock will be rejected. Two exception to
 *     these rules are: if the container to be locked currently has no other
 *     locks, or if the call isn't blocking and it's for a write lock.
 *
 *   - 'lock_auth <r_lock>': This auth. type allows the caller to hold multiple
 *     read locks, but no write locks. Note that if another thread is waiting
 *     for a write lock on the container and the caller already has a read lock
 *     then the lock will be rejected.
 *
 *   - 'lock_auth <w_lock>': This auth. type allows the caller to hold no more
 *     than one lock at a time, regardless of lock type. This is the default
 *     behavior of 'lock_auth' when it isn't specialized for a lock type. An
 *     exception to this behavior is if the container to be locked currently has
 *     no other locks.
 *
 *   - 'lock_auth <broken_lock>': This auth. type doesn't allow the caller to
 *     obtain any locks.
 *
 * If you want both deadlock prevention and the ability for threads to hold
 * a write lock plus one or more other locks at the same time, you can create a
 * 'null_container' for use by all threads when obtaining locks for any object.
 * The behavior will be transparent until a thread requests a "multi-lock" by
 * attempting to obtain a write lock on the 'null_container'. This will block
 * all future locks on the objects, allowing the thread in question to lock as
 * many objects as it needs to. To access this behavior, use 'get_multi' and
 * 'get_multi_const' instead of 'get_auth' and 'get_auth_const', passing the
 * 'null_container' as the first argument.
 *
 * Other notes:
 *
 *   - You must enable C++11 (or higher) when using this header.
 *
 *   - You might need to link your executable with libpthread after compiling.
 *
 *   - The assignment operators for 'locking_container' can cause assertions if
 *     the container can't be locked. The assignment operators should therefore
 *     only be used if there is no logical behavior other than an assertion if
 *     assignment fails.
 */


/*! \file locking-container.hpp
 *  \brief C++ container for data protection in multithreaded programs.
 *  \author Kevin P. Barry
 */

#ifndef locking_container_hpp
#define locking_container_hpp

#include <atomic>
#include <memory>

#include <pthread.h>
#include <assert.h>
#include <unistd.h>


/*! \class lock_auth_base
    \brief Base class for lock authorization classes.
    @see lock_auth
 */

class lock_base;

class lock_auth_base {
public:
  virtual bool lock_allowed(bool Read, bool Block = true) const = 0;

  virtual inline ~lock_auth_base() {}

private:
  friend class lock_base;

  virtual bool register_auth(bool Read, bool Block, bool LockOut, bool InUse, bool TestAuth = false) = 0;
  virtual void release_auth(bool Read) = 0;
};


class null_container_base {
  template <class> friend class locking_container_base;
  virtual lock_base *get_lock_object() = 0;
};


/*! \class locking_container_base
    \brief Base class for \ref locking_container.
 */

template <class> class object_proxy;

template <class Type>
class locking_container_base {
public:
  typedef Type                             type;
  typedef object_proxy <type>              proxy;
  typedef object_proxy <const type>        const_proxy;
  typedef std::shared_ptr <lock_auth_base> auth_type;

  inline proxy get(bool Block = true) {
    return this->get_auth(NULL, Block);
  }

  inline const_proxy get_const(bool Block = true) const {
    return this->get_auth_const(NULL, Block);
  }

  inline proxy get_auth(auth_type &Authorization, bool Block = true) {
    return this->get_auth(Authorization.get(), Block);
  }

  inline const_proxy get_auth_const(auth_type &Authorization, bool Block = true) const {
    return this->get_auth_const(Authorization.get(), Block);
  }

  virtual proxy       get_auth(lock_auth_base *Authorization, bool Block = true)             = 0;
  virtual const_proxy get_auth_const(lock_auth_base *Authorization, bool Block = true) const = 0;

  inline proxy get_multi(null_container_base &Multi, lock_auth_base *Authorization, bool Block = true) {
    return this->get_multi(Multi.get_lock_object(), Authorization, Block);
  }

  inline const_proxy get_multi_const(null_container_base &Multi, lock_auth_base *Authorization, bool Block = true) const {
    return this->get_multi_const(Multi.get_lock_object(), Authorization, Block);
  }

  inline proxy get_multi(null_container_base &Multi, auth_type &Authorization, bool Block = true) {
    return this->get_multi(Multi, Authorization.get(), Block);
  }

  inline const_proxy get_multi_const(null_container_base &Multi, auth_type &Authorization, bool Block = true) const {
    return this->get_multi_const(Multi, Authorization.get(), Block);
  }

  virtual auth_type get_new_auth() const {
    return auth_type();
  }

protected:
  virtual proxy get_multi(lock_base */*Multi*/, lock_auth_base */*Authorization*/, bool /*Block*/) {
    return proxy();
  }

  virtual const_proxy get_multi_const(lock_base */*Multi*/, lock_auth_base */*Authorization*/, bool /*Block*/) const {
    return const_proxy();
  }

  virtual inline ~locking_container_base() {}
};


/*! \class locking_container
    \brief C++ container class with automatic unlocking, concurrent reads, and
    deadlock prevention.

    Each instance of this class contains a lock and an encapsulated object of
    the type denoted by the template parameter. The \ref locking_container::get
    and \ref locking_container::get_const functions provide a proxy object (see
    \ref object_proxy) that automatically locks and unlocks the lock to simplify
    code that accesses the encapsulated object.
    \attention This class contains a mutable member, which means that a memory
    page containing even a const instance should not be remapped as read-only.
    \note This is not a "container" in the STL sense.
 */

class rw_lock;
template <class> class lock_auth;

template <class Type, class Lock = rw_lock>
class locking_container : public locking_container_base <Type> {
private:
  typedef lock_auth <Lock> auth_base_type;

public:
  typedef locking_container_base <Type> base;
  using typename base::type;
  using typename base::proxy;
  using typename base::const_proxy;
  using typename base::auth_type;
  //NOTE: this is needed so that the 'lock_auth_base' variants are pulled in
  using base::get_auth;
  using base::get_auth_const;
  using base::get_multi;
  using base::get_multi_const;

  /*! \brief Constructor.
   *
   * \param Object Object to copy as contained object.
   */
  explicit locking_container(const type &Object = type()) : contained(Object) {}

  /*! \brief Copy constructor.
   *
   * \param Copy Instance to copy.
   * \attention This function will block if "Copy" is locked.
   */
  explicit __attribute__ ((deprecated)) locking_container(const locking_container &Copy) : contained() {
    auto_copy(Copy, contained);
  }

  /*! Generalized version of copy constructor.*/
  explicit __attribute__ ((deprecated)) locking_container(const base &Copy) : contained() {
    auto_copy(Copy, contained);
  }

  /*! \brief Assignment operator.
   *
   * \param Copy Instance to copy.
   * \attention This function will block if the lock for either object is
   * locked. The lock for "Copy" is locked first, then the lock for this object.
   * The lock for "Copy" will remain locked until the locking of the lock for
   * this object is either succeeds or fails.
   * \attention This will cause an assertion if the lock can't be locked.
   *
   * \return *this
   */
  locking_container __attribute__ ((deprecated)) &operator = (const locking_container &Copy) {
    if (&Copy == this) return *this; //(prevents deadlock when copying self)
    return this->operator = (static_cast <const base&> (Copy));
  }

  /*! Generalized version of \ref locking_container::operator=.*/
  locking_container __attribute__ ((deprecated)) &operator = (const base &Copy) {
    proxy self = this->get();
    assert(self);
    if (!auto_copy(Copy, *self)) assert(NULL);
    return *this;
  }

  /*! Object version of \ref locking_container::operator=.*/
  locking_container __attribute__ ((deprecated)) &operator = (const Type &Object) {
    proxy self = this->get();
    assert(self);
    *self = Object;
    return *this;
  }

  /*! \brief Destructor.
   *
   * \attention This will block if the container is locked.
   */
  ~locking_container() {
    this->get();
  }

  /** @name Accessor Functions
  *
  */
  //@{

  /*! \brief Retrieve a proxy to the contained object.
   *
   * @see object_proxy
   * \attention Always check that the returned object contains a valid
   * pointer with object_proxy::operator!. The reference will always be
   * invalid if a lock hasn't been obtained.
   * \attention The returned object should only be passed by value, and it
   * should only be passed within the same thread that
   * \ref locking_container::get was called from. This is because the proxy
   * object uses reference counting that isn't reentrant.
   * \param Authorization Authorization object to prevent deadlocks.
   * \param Block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline proxy get_auth(lock_auth_base *Authorization, bool Block = true) {
    return this->get_multi(NULL, Authorization, Block);
  }

  /*! Const version of \ref locking_container::get.*/
  inline const_proxy get_auth_const(lock_auth_base *Authorization, bool Block = true) const {
    return this->get_multi_const(NULL, Authorization, Block);
  }

  //@}

  /*! Get a new authorization object.*/
  virtual auth_type get_new_auth() const {
    return locking_container::new_auth();
  }

  /*! Get a new authorization object.*/
  static auth_type new_auth() {
    return auth_type(new auth_base_type);
  }

private:
  inline proxy get_multi(lock_base *Multi, lock_auth_base *Authorization, bool Block = true) {
    //NOTE: no read/write choice is given here!
    return proxy(&contained, &locks, Authorization, Block, Multi);
  }

  inline const_proxy get_multi_const(lock_base *Multi, lock_auth_base *Authorization, bool Block = true) const {
    return const_proxy(&contained, &locks, Authorization, true, Block, Multi);
  }

  static inline bool auto_copy(const base &copied, type &copy) {
    typename base::const_proxy object = copied.get();
    if (!object) return false;
    copy = *object;
    return true;
  }

  type         contained;
  mutable Lock locks;
};


class lock_base {
public:
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) = 0;
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int unlock(lock_auth_base *auth, bool read) = 0;

protected:
  static inline bool register_auth(lock_auth_base *auth, bool Read, bool Block, bool LockOut, bool InUse, bool TestAuth) {
    return auth? auth->register_auth(Read, Block, LockOut, InUse, TestAuth) : true;
  }

  static inline void release_auth(lock_auth_base *auth, bool Read) {
    if (auth) auth->release_auth(Read);
  }
};


/*! \class rw_lock
    \brief Lock object that allows multiple readers at once.
 */

class rw_lock : public lock_base {
public:
  rw_lock() : readers(0), readers_waiting(0), writer(false), writer_waiting(false) {
    pthread_mutex_init(&master_lock, NULL);
    pthread_cond_init(&read_wait, NULL);
    pthread_cond_init(&write_wait, NULL);
  }

private:
  rw_lock(const rw_lock&);
  rw_lock &operator = (const rw_lock&);

public:
  int lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    //make sure this is an authorized lock type for the caller
    if (!register_auth(auth, read, block, writer_waiting, writer || readers, test)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    //check for blocking behavior
    bool must_block = writer || writer_waiting || (!read && readers);
    if (!block && must_block) {
      if (!test) release_auth(auth, read);
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    if (read) {
      //get a read lock
      ++readers_waiting;
      //NOTE: 'auth' is expected to prevent a deadlock if the caller already has
      //a read lock and there is a writer waiting
      while (writer || writer_waiting) {
        if (pthread_cond_wait(&read_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, read);
          --readers_waiting;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      --readers_waiting;
      int new_readers = ++readers;
      //if for some strange reason there's an overflow...
      assert(!writer && !writer_waiting && readers > 0);
      pthread_mutex_unlock(&master_lock);
      return new_readers;
    } else {
      //if the caller isn't the first in line for writing, wait until it is
      ++readers_waiting;
      while (writer_waiting) {
        //NOTE: use 'read_wait' here, since that's what a write unlock broadcasts on
        //NOTE: another thread should be blocking in 'write_wait' below
        if (pthread_cond_wait(&read_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, read);
          --readers_waiting;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      --readers_waiting;
      writer_waiting = true;
      //get a write lock
      while (writer || readers) {
        if (pthread_cond_wait(&write_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, read);
          writer_waiting = false;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      writer_waiting = false;
      writer = true;
      pthread_mutex_unlock(&master_lock);
      return 0;
    }
  }

  int unlock(lock_auth_base *auth, bool read) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    release_auth(auth, read);
    if (read) {
      assert(!writer && readers > 0);
      int new_readers = --readers;
      if (!new_readers && writer_waiting) {
        pthread_cond_broadcast(&write_wait);
      }
      pthread_mutex_unlock(&master_lock);
      return new_readers;
    } else {
      assert(writer && !readers);
      writer = false;
      if (writer_waiting) {
        pthread_cond_broadcast(&write_wait);
      }
      if (readers_waiting) {
        pthread_cond_broadcast(&read_wait);
      }
      pthread_mutex_unlock(&master_lock);
      return 0;
    }
  }

  ~rw_lock() {
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&read_wait);
    pthread_cond_destroy(&write_wait);
  }

private:
  int             readers, readers_waiting;
  bool            writer, writer_waiting;
  pthread_mutex_t master_lock;
  pthread_cond_t  read_wait, write_wait;
};


/*! \class w_lock
    \brief Lock object that allows only one thread access at a time.
 */

class w_lock : public lock_base {
public:
  w_lock() : locked(false) {
    pthread_mutex_init(&write_lock, NULL);
  }

private:
  w_lock(const w_lock&);
  w_lock &operator = (const w_lock&);

public:
  int lock(lock_auth_base *auth, bool /*read*/, bool block = true, bool test = false) {
    //NOTE: 'false' is passed instead of 'read' because this can lock out other readers
    if (!register_auth(auth, false, block, locked, locked, test)) return -1;
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&write_lock) != 0) {
      if (!test) release_auth(auth, false);
      return -1;
    }
    assert(!locked);
    locked = true;
    return 0;
  }

  int unlock(lock_auth_base *auth, bool /*read*/) {
    release_auth(auth, false);
    assert(locked);
    locked = false;
    return (pthread_mutex_unlock(&write_lock) == 0)? 0 : -1;
  }

  ~w_lock() {
    pthread_mutex_destroy(&write_lock);
  }

private:
  bool locked;
  pthread_mutex_t write_lock;
};


/*! \class r_lock
    \brief Lock object that allows multiple readers but no writers.
 */

class r_lock : public lock_base {
public:
  r_lock() : counter(0) {}

private:
  r_lock(const r_lock&);
  r_lock &operator = (const r_lock&);

public:
  int lock(lock_auth_base *auth, bool read, bool /*block*/ = true, bool test = false) {
    if (!read) return -1;
    if (!register_auth(auth, read, false, false, false, test)) return -1;
    //NOTE: this should be atomic
    int new_counter = ++counter;
    //(check the copy!)
    assert(new_counter > 0);
    return new_counter;
  }

  int unlock(lock_auth_base *auth, bool read) {
    if (!read) return -1;
    release_auth(auth, read);
    //NOTE: this should be atomic
    int new_counter = --counter;
    //(check the copy!)
    assert(new_counter >= 0);
    return new_counter;
  }

private:
  std::atomic <int> counter;
};


/*! \class broken_lock
    \brief Lock object that is permanently broken.
 */

struct broken_lock : public lock_base {
  int lock(lock_auth_base* /*auth*/, bool /*read*/, bool /*block*/ = true, bool /*test*/ = false) { return -1; }
  int unlock(lock_auth_base* /*auth*/, bool /*read*/) { return -1; }
};


/*! \class lock_auth
    \brief Lock authorization object.
    @see locking_container::auth_type
    @see locking_container::get_new_auth
    @see locking_container::get_auth
    @see locking_container::get_auth_const

    This class is used by \ref locking_container to prevent deadlocks. To
    prevent deadlocks, create one \ref lock_auth instance per thread, and pass
    it to the \ref locking_container when getting a proxy object. This will
    prevent the thread from obtaining an new incompatible lock type when it
    already holds a lock.
 */

template <class>
class lock_auth : public lock_auth_base {
public:
  lock_auth() : writing(0) {}

  bool lock_allowed(bool /*Read*/, bool /*Block*/ = true) const {
    return !writing;
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool /*Read*/, bool /*Block*/, bool /*LockOut*/, bool InUse, bool TestAuth) {
    if (writing && InUse) return false;
    if (TestAuth) return true;
    ++writing;
    assert(writing > 0);
    return true;
  }

  void release_auth(bool /*Read*/) {
    assert(writing > 0);
    --writing;
  }

  int writing;
};

template <>
class lock_auth <rw_lock> : public lock_auth_base {
public:
  lock_auth() : reading(0), writing(0) {}

  bool lock_allowed(bool Read, bool Block = true) const {
    if (!Block && !Read) return true;
    if (Read) return !writing;
    else      return !writing && !reading;
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool Read, bool Block, bool LockOut, bool InUse, bool TestAuth) {
    if (!Block && !Read)                 return true;
    if (writing && InUse)                return false;
    if (reading && !Read && InUse)       return false;
    if ((reading || writing) && LockOut) return false;
    if (TestAuth) return true;
    if (Read) {
      ++reading;
      assert(reading > 0);
    } else {
      ++writing;
      assert(writing > 0);
    }
    return true;
  }

  void release_auth(bool Read) {
    if (Read) {
      //NOTE: don't check 'writing' because there are a few exceptions!
      assert(reading > 0);
      --reading;
    } else {
      //NOTE: don't check 'reading' because there are a few exceptions!
      assert(writing > 0);
      --writing;
    }
  }

  int  reading, writing;
};

template <>
class lock_auth <r_lock> : public lock_auth_base {
public:
  lock_auth() : reading(0) {}

  bool lock_allowed(bool Read, bool /*Block*/ = true) const { return Read; }

private:
  bool register_auth(bool Read, bool /*Block*/, bool LockOut, bool /*InUse*/, bool TestAuth) {
    if (!Read)              return false;
    if (reading && LockOut) return false;
    if (TestAuth) return true;
    ++reading;
    assert(reading > 0);
    return true;
  }

  void release_auth(bool Read) {
    assert(Read);
    assert(reading > 0);
    --reading;
  }

  int  reading;
};

template <>
class lock_auth <broken_lock> : public lock_auth_base {
public:
  bool lock_allowed(bool /*Read*/, bool /*Block*/ = true) const { return false; }

private:
  bool register_auth(bool /*Read*/, bool /*Block*/, bool /*LockOut*/, bool /*InUse*/, bool /*TestAuth*/) { return false; }
  void release_auth(bool /*Read*/) { assert(false); }
};


template <class Type>
class object_proxy_base {
private:
  class locker;
  typedef std::shared_ptr <locker> lock_type;

public:
  object_proxy_base() {}

  object_proxy_base(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
    bool new_read, bool block, lock_base *new_multi) :
    container_lock(new locker(new_pointer, new_locks, new_auth, new_read, block, new_multi)) {}

  inline int last_lock_count() const {
    //(mostly provided for debugging)
    return container_lock? container_lock->lock_count : 0;
  }

protected:
  inline void opt_out() {
    container_lock.reset();
  }

  inline Type *pointer() {
    return container_lock? container_lock->pointer : 0;
  }

  inline Type *pointer() const {
    return container_lock? container_lock->pointer : 0;
  }

private:
  class locker {
  public:
    locker() : pointer(NULL), lock_count(), read(true), locks(NULL), multi(NULL), auth() {}

    locker(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
      bool new_read, bool block, lock_base *new_multi) :
      pointer(new_pointer), lock_count(), read(new_read), locks(new_locks), multi(new_multi), auth(new_auth) {
      //attempt to lock the multi-lock if there is one (not counted toward 'auth')
      if (multi && multi->lock(auth, true, block, true) < 0) this->opt_out(false, false);
      //attempt to lock the container's lock
      if (!locks || (lock_count = locks->lock(auth, read, block)) < 0) this->opt_out(false);
    }

    int last_lock_count() const {
      //(mostly provided for debugging)
      return lock_count;
    }

    bool read_only() const {
      return read;
    }

    void opt_out(bool unlock1, bool unlock2 = true) {
      pointer    = NULL;
      lock_count = 0;
      if (unlock1 && locks) locks->unlock(auth, read);
      //NOTE: pass 'NULL" as authorization because the lock wasn't recorded
      if (unlock2 && multi) multi->unlock(NULL, true);
      auth  = NULL;
      locks = NULL;
      multi = NULL;
    }

    inline ~locker() {
      this->opt_out(true);
    }

    Type *pointer;
    int   lock_count;

  private:
    locker(const locker&);
    locker &operator = (const locker&);

    bool             read;
    lock_base       *locks, *multi;
    lock_auth_base  *auth;
  };

  lock_type container_lock;
};


/*! \class object_proxy
    \brief Proxy object for \ref locking_container access.

    Instances of this class are returned by \ref locking_container instances as
    proxy objects that access the contained object. \ref locking_container is
    locked upon return of this object and references to the returned object are
    counted as it's copied. Upon destruction of the last reference the container
    is unlocked.
 */

template <class Type>
class object_proxy : public object_proxy_base <Type> {
private:
  template <class, class> friend class locking_container;

  object_proxy(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
    bool block, lock_base *new_multi) :
    object_proxy_base <Type> (new_pointer, new_locks, new_auth, false, block, new_multi) {}

public:
  object_proxy() : object_proxy_base <Type> () {}

  /** @name Checking Referred-to Object
  *
  */
  //@{

  /*! \brief Clear the reference and unlock the container.
   *
   * The container isn't unlocked until the last reference is destructed. This
   * will clear the reference for this object alone and decrement the
   * reference count by one. If the new reference count is zero then the
   * container is unlocked.
   *
   * \return *this
   */
  inline object_proxy &clear() {
    this->opt_out();
    return *this;
  }

  /*! \brief Check if the reference is valid.
   *
   * \return valid (true) or invalid (false)
   */
  inline operator bool() const {
    return this->pointer();
  }

  /*! \brief Check if the reference is invalid.
   *
   * \return invalid (true) or valid (false)
   */
  inline bool operator ! () const {
    return !this->pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const object_proxy &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const object_proxy <const Type> &equal) const {
    return this->pointer() == equal.pointer();
  }

  //@}

  /** @name Trivial Iterator Functions
  *
  */
  //@{

  inline operator       Type*()          { return  this->pointer(); }
  inline operator const Type*() const    { return  this->pointer(); }
  inline       Type &operator *()        { return *this->pointer(); }
  inline const Type &operator *() const  { return *this->pointer(); }
  inline       Type *operator ->()       { return  this->pointer(); }
  inline const Type *operator ->() const { return  this->pointer(); }

  //@}
};


template <class Type>
class object_proxy <const Type> : public object_proxy_base <const Type> {
private:
  template <class, class> friend class locking_container;

  object_proxy(const Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
    bool read, bool block, lock_base *new_multi) :
    object_proxy_base <const Type> (new_pointer, new_locks, new_auth, read, block, new_multi) {}

public:
  object_proxy() : object_proxy_base <const Type> () {}

  /** @name Checking Referred-to Object
  *
  */
  //@{

  /*! \brief Clear the reference and unlock the container.
   *
   * The container isn't unlocked until the last reference is destructed. This
   * will clear the reference for this object alone and decrement the
   * reference count by one. If the new reference count is zero then the
   * container is unlocked.
   *
   * \return *this
   */
  inline object_proxy &clear() {
    this->opt_out();
    return *this;
  }

  /*! \brief Check if the reference is valid.
   *
   * \return valid (true) or invalid (false)
   */
  inline operator bool() const {
    return this->pointer();
  }

  /*! \brief Check if the reference is invalid.
   *
   * \return invalid (true) or valid (false)
   */
  inline bool operator ! () const {
    return !this->pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const object_proxy &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const object_proxy <Type> &equal) const {
    return this->pointer() == equal.pointer();
  }

  //@}

  /** @name Trivial Iterator Functions
  *
  */
  //@{

  inline operator const Type*() const    { return  this->pointer(); }
  inline const Type &operator *() const  { return *this->pointer(); }
  inline const Type *operator ->() const { return  this->pointer(); }

  //@}
};


class null_container;

template <>
class object_proxy <void> : public object_proxy_base <void> {
private:
  friend class null_container;

  object_proxy(bool value, lock_base *new_locks, lock_auth_base *new_auth, bool block, lock_base *new_multi) :
    object_proxy_base <void> ((void*) value, new_locks, new_auth, false, block, new_multi) {}

public:
  object_proxy() : object_proxy_base <void> () {}

  inline object_proxy &clear() {
    this->opt_out();
    return *this;
  }

  inline operator bool() const {
    return this->pointer();
  }

  inline bool operator ! () const {
    return !this->pointer();
  }
};


/*! \class null_container
    \brief Empty container, used as a global locking mechanism.
 */

class null_container : public null_container_base {
private:
  typedef lock_auth <rw_lock> auth_base_type;

public:
  typedef object_proxy <void>              proxy;
  typedef std::shared_ptr <lock_auth_base> auth_type;

  ~null_container() {
    this->get_auth(NULL);
  }

  inline proxy get_auth(auth_type &Authorization, bool Block = true) {
    return this->get_auth(Authorization.get(), Block);
  }

  inline proxy get_auth(lock_auth_base *Authorization, bool Block = true) {
    return proxy(true, &locks, Authorization, Block, NULL);
  }

private:
  inline lock_base *get_lock_object() {
    return &locks;
  }

  mutable rw_lock locks;
};

#endif //locking_container_hpp