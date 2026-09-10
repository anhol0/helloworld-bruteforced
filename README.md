# What is it and why does it exist?

Operating systems with the Linux kernel under the hood are incredibly transparent about their memory.

This is why this idea came to my mind:

> What if we create a function, but then call it without knowing where it is?

You may ask:

> "anhol, are you stupid? How can you call a function without knowing where it is?"

Reasonable enough. Though, let me explain.

Even if a function is never called normally, it can still end up mapped into the process's executable memory.

> **Note:** This is not always true. Compilers and linkers may remove unused functions entirely, especially when optimizations, LTO, or section garbage collection are enabled. In this example, the function is explicitly kept in the binary.

Therefore, we can find the executable memory region that the kernel mapped for our program, iterate over every possible byte offset inside it, and brute-force the address pointing to our beloved `hello_world` function.

So that's exactly what I did in this simple example.

## How does it work?

1. **Retrieve the executable memory region**

   First, we retrieve the addresses of the executable memory mappings from:

   ```text
   /proc/self/maps
   ```

   This tells us where the executable part of our program is currently mapped in virtual memory.

2. **Iterate over every address and create a fork**

   After that, we iterate over every byte address inside the executable region and create a child process using `fork()` for each attempt.

   > **Note 1:** Forking is needed so we can track exit codes without worrying about crashes, segmentation faults, illegal instructions, and other unpleasant things. It also isolates memory modifications made by the child from the parent thanks to copy-on-write, so accidentally corrupting the child's memory doesn't destroy the scanner itself.

   > **Note 2:** We also enable strict `seccomp` to minimize the damage caused by executing arbitrary code. The child can still execute normal user-space instructions and functions, but if that code attempts a syscall outside the tiny set permitted by strict seccomp — such as `read`, `write`, `exit`, and signal return — the kernel kills the child.
   >
   > Without this restriction, an arbitrary instruction sequence could potentially wander into other parts of the program and perform operations we didn't intend. For example, it might accidentally reach `main()` or some other function and continue executing instead of terminating normally.

3. **Cast each address to a function pointer**

   We know the signature of the function we're looking for:

   ```c
   int fn(void);
   ```

   So we cast every memory address we iterate over to a function pointer with that signature:

   ```c
   int (*fn)(void) = (int (*)(void))addr;
   ```

4. **Call it and see what happens**

   Then we call it.

   Since we're jumping to every possible byte offset, most addresses obviously don't point to the beginning of a valid function.

   They may:

   * crash with a segmentation fault;
   * execute an illegal instruction;
   * get killed by `seccomp`;
   * return some random value;
   * enter an infinite loop;
   * or otherwise behave unpredictably.

   If a child takes longer than we're willing to wait, we simply kill it and move on to the next address.

   > **Note:** Keeping track of timeouts is essential because some arbitrary instruction sequences may result in infinite loops or other hangs.

5. **Check the exit code**

   If the child successfully executes the function and its return value is what we expect:

   ```text
   69
   ```

   we make the child exit with that value.

   The parent process sees exit code `69` and knows that we're lucky:

   **we successfully brute-forced our own executable memory and found `hello_world`.**

   >  **Note:** Yhe address we found might point in the middle of the function so I added a check. We use `-rdynamic` flag that instructs liker to add ALL the symbols to the dynamic symbol table. This way our address found can atually be tested. If the `info.dli_sname` is empty, but 69 was returned - it was either a false positive OR we jumped inside of the function itself and started execution from the middle. In these cases - we just keep going until we actually find a symbol. The final output should look like: 
   > ``` 
   > probe child pid=<PID> addr=<ADDRESS>
   > Hello world
   > FOUND: <SAME ADDRESS>
   > Function name: hello_world
   > Symbol start:  <SAME ADDRESS> 
   > ```


## Why is it needed?

Actually, I have no idea.

It might be useful during penetration testing or simply as an experiment for understanding process memory, executable mappings and function pointers.

On Linux, processes running under the same user may, depending on the system's security configuration, be able to inspect or interact with each other's memory through mechanisms such as `ptrace`.

In certain circumstances, abusing access to another process's memory could potentially become a security issue or contribute to privilege escalation. If an attacker gets sufficient control over the process, it can be able to alter its execution. However, Linux places heavy restrictions on doing so, and, thankfully, becomes more and more secure each update.

So, for now, this project mostly exists because brute-forcing the location of your own `hello_world()` function sounded funny.
