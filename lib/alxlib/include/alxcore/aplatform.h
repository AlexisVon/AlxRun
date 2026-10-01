/*****************************************************************/ /**
 * \file   aplatform.h
 * \brief  Cross-platform adaptation layer (Windows/Linux)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_WINDOWS_H_
#define _ALEXIS_WINDOWS_H_

#include "abase.h"
#include "abytes.h"
#include "astring.h"
#include "avariant.h"

#include <atomic>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#    include <Windows.h>
#    include <psapi.h>
#    include <tlhelp32.h>
#    pragma warning(disable : 4251)
#else
#    include <dirent.h>
#    include <dlfcn.h>
#    include <fcntl.h>
#    include <fnmatch.h>
#    include <link.h>
#    include <pthread.h>
#    include <semaphore.h>
#    include <sys/mman.h>
#    include <sys/prctl.h>
#    include <sys/resource.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace alx {
    /// Named shared-memory segment with a companion lock -- see the definition below
    class ALXCORE_API mmap;
    /// Snapshot of one process -- see the definition below
    class ALXCORE_API process_info;
    /// Owns one running child process -- see the definition below
    class ALXCORE_API process_ctrl;

    /**
     * \brief Outcome of one exec_sync() / exec_async() run
     *
     * success is not the exit status: it says the child was started, its output captured and its
     * exit status read, so a command that ran and exited non-zero is still a success. A command
     * that could not be started at all is reported with success = false and the reason in output.
     */
    class ALXCORE_API exec_result {
    public:
        /// Build a result; the defaults describe a run that never started
        exec_result(bool _success = false, bool _timeout = false, int_32 _excode = -1, const std::string& _output = std::string())
            : success(_success), timeout(_timeout), excode(_excode), output(_output) {
        }

    public:
        /// Child started, its output captured and its exit status read
        bool success = false;
        /// Child killed for running past _mill_wait
        bool timeout = false;
        /// Exit status of the child; -1 when it did not exit normally (POSIX: killed by a signal)
        int_32 excode = -1;
        /// Merged stdout and stderr of the child, in arrival order; the reason on a start failure
        std::string output;
    };

    /**
     * \brief Run a command and wait for it to finish, capturing its output
     *
     * Same as the three-argument form, with the working directory left at ".". On Linux the
     * command goes through /bin/sh, so it may be any shell command line.
     *
     * \param _cmd Command line; Linux hands it to /bin/sh -c, Windows to CreateProcessA
     * \param _mill_wait Max run time [ms]; past it the child is killed
     * \return success = started and reaped, excode = its exit status, output = stdout + stderr
     */
    exec_result ALXCORE_API exec_sync(const std::string& _cmd, const uint_32 _mill_wait = 500);

    /**
     * \brief Run a command in a directory and wait for it to finish, capturing its output
     *
     * stdout and stderr are merged into output. An empty _cmd returns success at once on either
     * platform, with exit code 0 and no output.
     *
     * \param _cmd Command line; Linux hands it to /bin/sh -c, Windows to CreateProcessA
     * \param _dir Working directory of the child. Linux makes a directory the child cannot enter
     *             show up as exit code 1; Windows fails the call itself instead
     * \param _mill_wait Max run time [ms], checked in 60 ms steps; past it the child is killed
     *                   (Linux: SIGTERM, Windows: TerminateProcess) and timeout is set
     * \return success = started and reaped, excode = its exit status, output = stdout + stderr
     */
    exec_result ALXCORE_API exec_sync(const std::string& _cmd, const std::string& _dir, const uint_32 _mill_wait = 500);

    /**
     * \brief Run the matching exec_sync() on a worker thread and return at once
     *
     * The command starts before this returns, and _mill_wait means what it means in exec_sync().
     * Destroying the future without waiting on it blocks until the command is done.
     */
    std::future<exec_result> ALXCORE_API exec_async(const std::string& _cmd, const uint_32 _mill_wait = 500);

    /// Run the matching exec_sync() in a directory on a worker thread and return at once
    std::future<exec_result> ALXCORE_API exec_async(const std::string& _cmd, const std::string& _dir, const uint_32 _mill_wait = 500);

    /**
     * \brief Block the calling thread for a span of time
     *
     * The span is a floor, not a promise: the platform's timer resolution and the scheduler can
     * only stretch it. Linux resumes the remainder of the sleep when a signal interrupts it.
     */
    void ALXCORE_API high_precision_sleep(uint_64 _ms);
#ifdef _WIN32

    /**
     * \brief Read the version resource of the file at a path
     *
     * \param _path Path of the DLL or EXE
     * \return {major, minor, build, revision} out of the fixed file info; empty when the file
     *         carries no version resource or it cannot be read
     */
    std::vector<uint_32> ALXCORE_API dll_version(const std::string& _path);

    /**
     * \brief Read an RT_RCDATA resource out of the executable of this process
     *
     * \return Copy of the resource bytes; empty when there is no such resource, or it cannot be
     *         loaded or is empty
     */
    bytes ALXCORE_API load_resource_data(uint_32 _id);

    /**
     * \brief Capture a visible window of a process as a bitmap
     *
     * \param _pid Process whose first visible top-level window is captured
     * \return Newly created HBITMAP that the caller owns and must release with DeleteObject();
     *         nullptr when the process has no visible window or the capture failed
     */
    HBITMAP ALXCORE_API snap_process_window(uint_32 _pid);
#endif
}

/**
 * \brief Named shared memory together with a lock that lives beside it
 *
 * open() creates the segment or attaches to an existing one by name; afterwards, areas of it are
 * reached by offset through set/read/write/take, and every one of those calls runs under a
 * companion lock (POSIX: named semaphore, Windows: named mutex). Two processes that opened the
 * same name therefore see a consistent buffer, while the segment outlives them all -- close()
 * only detaches this object, destroy() removes the name.
 *
 * open(), close() and destroy() are serialized by convention: the caller must not run them
 * against each other, nor against a byte operation on the same object.
 */
class ALXCORE_API alx::mmap : public noncopyable {
public:

    /**
     * \brief Name the segment; nothing is mapped yet
     *
     * \param _name Name shared with the other holders, and the one the companion lock is named
     *              after; open() fails when it is empty
     */
    explicit mmap(const std::string& _name);

    /// Detach; a segment this object created is unlinked here, its companion lock included
    virtual ~mmap();

public:

    /**
     * \brief Create the segment, or attach to the one already under this name
     *
     * With _create, an existing name is attached to as it is (owner() stays false) and a free one
     * is made to hold _size bytes; without it the name must already hold at least _size bytes.
     * Already open is a no-op returning true. A failure leaves the object closed, and it may be
     * opened again.
     *
     * \param _size Bytes the segment must hold; 0 fails
     * \param _create Create the segment when the name is free; false requires it to exist
     * \return true when the segment is mapped into this process afterwards
     */
    bool open(uint_32 _size, bool _create);

    /// True while the segment is mapped into this process
    bool opened();

    /// True when this object's open() created the segment, which its destructor then unlinks
    bool owner() const;

    /**
     * \brief Detach from the segment, leaving it and its lock in place for the other holders
     *
     * The mapping, the descriptor and the lock are released and maxsize() drops back to 0, so
     * the name is still there for the next open(). No-op when not open.
     */
    void close();

    /**
     * \brief Detach and remove the name, together with its companion lock, for every holder
     *
     * close() first, then the removal; a name that is already gone counts as removed, so calling
     * this twice is harmless. On Windows the segment lives exactly as long as its last handle,
     * which leaves close() as the whole job.
     *
     * \return false when the object has no name, or when the removal was refused
     */
    bool destroy();

    /// Bytes the segment holds: _size as passed to open(), 0 while not open. On Windows it is
    /// the whole mapped region, which the system may have made larger than _size
    inline uint_32 maxsize() const { return size; }

    /**
     * \brief Store one byte at an offset
     *
     * \param _offset Offset from the start of the segment
     * \return false when the segment is not open, or the byte at _offset is past its end
     */
    bool set(uint_8 _value, uint_32 _offset);

    /**
     * \brief Fill an area with one byte
     *
     * \param _offset Offset from the start of the segment
     * \param _size Bytes to fill; 0 succeeds without touching anything
     * \return false when the segment is not open, or the area runs past its end
     */
    bool set(uint_8 _value, uint_32 _offset, uint_32 _size);

    /**
     * \brief Replace one byte when it still holds an expected value
     *
     * The test and the store happen under the companion lock, so this is a real compare-and-swap
     * towards every other holder of the name -- but not towards code that reached the segment by
     * other means.
     *
     * \param _expected Value the byte must still hold for the store to happen
     * \param _replace Value stored when it does
     * \param _offset Offset from the start of the segment
     * \return false when the byte differed, or when the segment is not open or _offset is past
     *         its end
     */
    bool cas(uint_8 _expected, uint_8 _replace, uint_32 _offset);

    /**
     * \brief Copy bytes from outside into the segment
     *
     * \param _source Bytes to copy from; _size of them are read
     * \param _offset Offset in the segment to start at
     * \param _size Bytes to copy; 0 succeeds without touching anything
     * \return false when the segment is not open, or the area runs past its end
     */
    bool write(const void* _source, uint_32 _offset, uint_32 _size);

    /**
     * \brief Copy bytes out of the segment
     *
     * \param _buffer Destination; _size bytes are written into it
     * \param _offset Offset in the segment to start at
     * \param _size Bytes to copy; 0 succeeds without touching anything
     * \return false when the segment is not open, or the area runs past its end
     */
    bool read(void* _buffer, uint_32 _offset, uint_32 _size);

    /**
     * \brief Read an area and zero it, in one locked step
     *
     * \param _buffer Destination; _size bytes are written into it
     * \param _offset Offset in the segment to start at
     * \param _size Bytes to take; 0 succeeds without touching anything
     * \return false when the segment is not open, or the area runs past its end
     */
    bool take(void* _buffer, uint_32 _offset, uint_32 _size);

    /// Zero the whole segment; no-op when not open
    void clear();

    /// Copy a view into the segment at _offset, taking the length from the view
    inline bool write(const bytes_view& _data, uint_32 _offset) { return write(_data.data(), _offset, (uint_32) _data.size()); }
    /// Copy the segment at _offset into a buffer that is already as long as the area to read
    inline bool read(bytes& _data, uint_32 _offset) { return read(_data.data(), _offset, (uint_32) _data.size()); }
    /// Read the area at _offset and zero it, the buffer sized as for read()
    inline bool take(bytes& _data, uint_32 _offset) { return take(_data.data(), _offset, (uint_32) _data.size()); }
    /// Read the whole segment into a new buffer; empty bytes when it is not open
    inline bytes read() {
        bytes result(size);
        return read(result, 0) ? result : bytes();
    }
    /// Read the whole segment into a new buffer, leaving the segment zeroed
    inline bytes take_all() {
        bytes result(size);
        return take(result, 0) ? result : bytes();
    }

protected:
    /// Name this object was constructed with; segment and companion lock are named after it
    std::string name;
    /// Bytes the segment holds; 0 while not open
    uint_32 size{0};
    /// Set by open() when this object created the segment, which unlinks it in the destructor
    bool created{false};
#ifdef _WIN32
    /// Scoped hold of the companion lock: taken in the constructor, released in the destructor
    class mmap_mutex_locker {
    public:
        /// Wait for the lock; a null lock is left alone, which is the state before open()
        explicit mmap_mutex_locker(HANDLE& _p_mutex_)
            : p_mutex(_p_mutex_) {
            if (nullptr != p_mutex) ::WaitForSingleObject(p_mutex, INFINITE);
        }
        /// Release it
        ~mmap_mutex_locker() {
            if (nullptr != p_mutex) ::ReleaseMutex(p_mutex);
        }

    private:
        HANDLE& p_mutex;
    };
    /// File mapping object of the segment; nullptr while not open
    HANDLE map{nullptr};
    /// Companion lock, held while an area of the segment is touched; nullptr while not open
    HANDLE pmutex{nullptr};
    /// Base address of the mapping; nullptr while not open, and what opened() tests
    LPVOID data{nullptr};
#else
    /// Scoped hold of the companion lock: taken in the constructor, released in the destructor
    class mmap_mutex_locker {
    public:
        /// Wait for the lock; a null lock is left alone, which is the state before open()
        explicit mmap_mutex_locker(sem_t*& _p_mutex_)
            : p_mutex(_p_mutex_) {
            if (nullptr != p_mutex) ::sem_wait(p_mutex);
        }
        /// Release it
        ~mmap_mutex_locker() {
            if (nullptr != p_mutex) ::sem_post(p_mutex);
        }

    private:
        sem_t*& p_mutex;
    };
    /// Descriptor of the segment; -1 while not open
    int fd{-1};
    /// Companion lock, held while an area of the segment is touched; nullptr while not open
    sem_t* pmutex{nullptr};
    /// Base address of the mapping; nullptr while not open, and what opened() tests
    void* data{nullptr};
#endif
};

/**
 * \brief Snapshot of one process: identity, parent, thread count, priority and image path
 *
 * Windows fills the fields from a toolhelp snapshot, Linux from /proc. What a lookup returns is a
 * copy of the values at that moment, not a handle: nothing stays attached to the process.
 */
class ALXCORE_API alx::process_info {
public:

    /**
     * \brief Snapshot this process
     *
     * \return This process; an empty snapshot when the values could not be read
     */
    static process_info current();

    /**
     * \brief Look a process up by pid
     *
     * \return The process, 0 or 1 entry; empty when no live process has that pid
     */
    static std::vector<process_info> find_process(uint_32 _pid);

    /**
     * \brief Find the processes whose image matches a name
     *
     * A name containing a path separator is compared against the whole path, any other name only
     * against the last component of it; both comparisons are case-sensitive. An empty name
     * returns every process.
     *
     * \return Every match, 0 to n entries
     */
    static std::vector<process_info> find_process(const std::string& _name);

    /**
     * \brief Scan the live processes and keep the ones a predicate accepts
     *
     * \param _hit Match test, called on the calling thread once per process; an empty
     *             std::function keeps every process
     * \return Every process _hit accepted, 0 to n entries
     */
    static std::vector<process_info> find_process(std::function<bool(const process_info&)> _hit = nullptr);

    /**
     * \brief Find the children of a process
     *
     * \return Every process whose ppid is _ppid, 0 to n entries; Linux reports the parent /proc
     *         shows at the time of the scan, which is the new one for a re-parented orphan
     */
    static std::vector<process_info> find_child_process(uint_32 _ppid);

public:
    /// Build an empty snapshot: pid 0, no parent, no path -- the "not found" value
    process_info() {}
    /// Process id; 0 in an empty snapshot
    const uint_32 pid{0};
    /// Pid of the parent: Linux reads what /proc reports now, Windows what the snapshot recorded
    const uint_32 ppid{0};
    /// Threads in the process when the snapshot was taken
    const uint_32 thread_cnt{0};
    /// Windows: the process's base priority class; Linux: the kernel static priority, 20 + nice
    const int_32 base_priority{0};
    /// Full image path; empty when the process exposes none, or when it cannot be read
    const std::string path;

private:
    static std::string base_name(const std::string& _path);
    static std::string process_path(uint_32 _pid);
#ifndef _WIN32
    static int_32 parse_int(const std::string& _str);
#endif
    static std::vector<process_info> read_process(uint_32 _pid);
#ifdef _WIN32
    process_info(DWORD _pid, DWORD _ppid, DWORD _t_cnt, LONG _b_pri, const std::string& _path);
#else
    process_info(pid_t _pid, pid_t _ppid, size_t _t_cnt, int _b_pri, const std::string& _path);
#endif
};

/**
 * \brief Owns one child process, its three pipes and the thread that drains them
 *
 * start() runs the executable the object was constructed with, and a reader thread delivers the
 * child's stdout and stderr through sig_output / sig_errput until both pipes hit end of file.
 * The child never outlives this object: the destructor kills it and joins the reader thread.
 *
 * The control calls -- start(), input(), terminate(), kill() -- are serialized by convention.
 */
class ALXCORE_API alx::process_ctrl : public noncopyable {
public:

    /// Outcome of the child process: how it ended, and with what
    class wait_status {
    public:
        /// True once the child has been reaped; false while it runs, and before the first start()
        bool exited = false;
        /// Exit status of the child; -1 until it is reaped, and when a signal took it instead
        int_32 exit_code = -1;
#ifndef _WIN32
        /// POSIX: true when a signal ended it rather than a normal exit
        bool signaled = false;
        /// POSIX: the signal that ended it; 0 when it exited normally
        int_32 term_sig = 0;
#endif
    };

    /// Limits imposed on the child by this process, all or nothing
    class process_limits {
    public:
        /**
         * \brief Build a limit set
         *
         * \param _mem_bytes Address-space cap [bytes]; 0 = unlimited
         * \param _kill_on_parent_exit Kill the child when this process exits
         */
        process_limits(uint_64 _mem_bytes = 0, bool _kill_on_parent_exit = false)
            : mem_bytes(_mem_bytes), kill_on_parent_exit(_kill_on_parent_exit) {
        }

    public:
        /// Address space the child may use [bytes]; 0 = unlimited. POSIX: the RLIMIT_AS of the
        /// child, Windows: the process memory limit of its job object
        uint_64 mem_bytes;
        /// True makes the child die with this process (POSIX: a death signal, Windows: the job
        /// object is closed with us)
        bool kill_on_parent_exit;
    };

    /**
     * \brief Bind to an executable; nothing runs yet
     *
     * \param _exec_path Path of the executable start() will run. Linux executes it directly, so
     *                  it must be a real path; Windows hands it to CreateProcessA
     */
    process_ctrl(std::string _exec_path) : p_exec(_exec_path) {}

    /**
     * \brief Start the child, with its pipes and its reader thread
     *
     * A child that is still running is killed first, and the previous outcome is cleared. A limit
     * that cannot be imposed is never dropped: the call fails instead.
     *
     * \param _args Arguments after the executable; Linux passes them through untouched, Windows
     *              quotes each one into the command line
     * \return true only when the child is running with every requested limit in place; false when
     *         the executable could not be run, which leaves no child behind
     */
    bool start(const std::vector<std::string>& _args = std::vector<std::string>(), const process_limits& _limits = process_limits());
    /// Kill the child and join the reader thread, blocking until it is gone
    ~process_ctrl();

    /**
     * \brief Write to the child's stdin
     *
     * The bytes go out as they are: no newline is added and there is nothing to flush.
     *
     * \return false when no child is running, when it has already exited, or when the pipe took
     *         only part of the string
     */
    bool input(const std::string& _string);

    /**
     * \brief Ask the child to stop, without waiting for it
     *
     * Linux sends SIGTERM. Windows has no way to signal it, so it closes the child's stdin -- the
     * usual shutdown for a console program, and no shutdown at all for one that ignores it.
     *
     * \return false when no child is running or it has already exited
     */
    bool terminate();

    /**
     * \brief Kill the child hard, reap it and release this handle
     *
     * Blocks until the child is gone and its reader thread has been joined. Called again, or
     * after the child exited on its own, it only does that cleanup. The outcome stays readable
     * through status(), with pid() back at 0.
     *
     * \return true when a live child was killed, false when there was none to kill
     */
    bool kill();

    /**
     * \brief Outcome of the child, reaping it when it has already exited
     *
     * Non-blocking: it samples the child and returns what is known by then.
     *
     * \return The outcome; exited = false means either that no child was started or that the
     *         running one has not been reaped yet, and exit_code is -1 either way
     */
    wait_status status();

    /**
     * \brief Block until the child exits, or until the deadline passes
     *
     * Samples status() every 5 ms, so an exit that happens while it waits is noticed.
     *
     * \param _mill Max wait [ms]; 0 only samples the current state
     * \return true when the child has exited
     */
    bool wait(const uint_32 _mill = 0);

    /**
     * \brief Whether the reader thread has drained both pipes of the last child
     *
     * True when no further sig_output / sig_errput callback can run: both pipes reached end of
     * file, or kill() cut them. True as well before the first start().
     *
     * \param _mill Max wait [ms]; 0 only samples the current state
     * \return true when the output of the last child is complete
     */
    bool output_done(const uint_32 _mill = 0);
    /// Pid of the child while it runs; 0 before start() and after kill()
    inline uint_32 pid() const { return p_id; }
#ifdef _WIN32
    /// Windows: handle of the child while it runs; nullptr otherwise
    inline HANDLE handle() const { return h_process; }
#endif
public:

    /// Child stdout, in chunks of at most 4096 bytes. The slot runs on the reader thread, where
    /// calling kill() or destroying the object would join that thread from itself
    signal<const std::string&> sig_output;

    /// Child stderr, delivered the same way
    signal<const std::string&> sig_errput;

private:
    void loop();
#ifndef _WIN32
    static wait_status decode_wait_status(int _status);
#endif

private:
    uint_32 p_id{0};
    wait_status p_status;
    std::atomic<bool> p_output_done{true};
    std::thread* t_loop{nullptr};
#ifdef _WIN32
    std::string p_exec;
    HANDLE h_process{nullptr};
    HANDLE h_job{nullptr};
    struct {
        HANDLE read{nullptr};
        HANDLE write{nullptr};
    } p_stdin, p_stdout, p_stderr;
#else
    std::string p_exec;
    pid_t h_process{-1};
    struct {
        /// The two ends of one pipe: [0] reads, [1] writes. The end this side does not use is
        /// closed once the child is forked, and left at -1
        int pipefd[2]{-1, -1};
    } p_stdin, p_stdout, p_stderr;
#endif
};

#endif