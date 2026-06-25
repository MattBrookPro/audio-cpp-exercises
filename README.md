# C++ / Audio Technical Test Answers

My worked answers to the seven questions, with reasoning shown and code you can compile.

A note up front: where a question is vague I've stated my assumptions inline, as requested.

The code answers (Questions 1, 2, 6 and 7) live in [`src/`](src/) and build with CMake. The Question 7 program also demonstrates the Question 5 hand-over. Questions 3, 4 and 5 are written answers.

## Build and run the code answers

```
cmake -B build -S .
cmake --build build
./build/q1_arithmetic_mean
./build/q2_parse_method_name
./build/q6_true_peak
./build/q7_audio_engine
```

---

## Question 1: Arithmetic mean

> Write a function that, given some numbers, returns the arithmetic mean of them. Demonstrate it working.

I'm returning a small struct with the mean plus a validity flag, so the empty case has a clear answer rather than dividing by zero or returning a misleading `0`.

```cpp
#include <vector>
#include <iostream>

struct MeanReturn
{
    float mean;
    bool isValid;
};

MeanReturn meanFinder(const std::vector<float>& numbers)
{
    MeanReturn result;

    if (numbers.empty())
    {
        result.isValid = false;
        result.mean = 0.0f;
        return result;
    }

    float runningTotal = 0.0f;
    for (float number : numbers)
        runningTotal += number;

    result.mean = runningTotal / numbers.size();
    result.isValid = true;
    return result;
}

int main()
{
    MeanReturn r = meanFinder({2.0f, 4.0f, 6.0f});
    if (r.isValid) std::cout << "Mean: " << r.mean << "\n";   // Mean: 4

    MeanReturn empty = meanFinder({});
    if (!empty.isValid) std::cout << "Empty collection - no mean\n";
}
```

I'm assuming floats are fine here. For a very large collection you'd accumulate into a `double` to reduce rounding error, but that's overkill for this.

---

## Question 2: Parse a test method name into English

> You are writing a JUnit-style test framework. The test method names take the form `testCowsCanBeMilked()`, displayed as "Cows can be milked". In C++, write a function that parses the method signature and returns the equivalent display version. Demonstrate it working. Well.

What I do is pull out just the method name from the signature, drop the `test` prefix, then split into words at each capital letter. The first word keeps its capital and the rest go lowercase.

```cpp
#include <string>
#include <vector>
#include <cctype>
#include <iostream>

std::string parseFunction(const std::string& signature)
{
    // grab just the method name - it sits between the last space and the '('
    std::size_t start = signature.find_last_of(' ');
    std::size_t end   = signature.find('(');
    std::string name  = signature.substr(start + 1, end - start - 1);

    // drop the "test" prefix
    const std::string prefix = "test";
    if (name.rfind(prefix, 0) == 0)
        name = name.substr(prefix.size());

    // split into words at each capital letter
    std::vector<std::string> words;
    std::string currentWord;
    for (char c : name)
    {
        if (std::isupper(c) && !currentWord.empty())
        {
            words.push_back(currentWord);
            currentWord.clear();
        }
        currentWord += c;
    }
    if (!currentWord.empty())
        words.push_back(currentWord);

    // first word keeps its capital, the rest go lowercase, joined by single spaces
    std::string result;
    for (int i = 0; i < words.size(); ++i)
    {
        std::string word = words[i];
        if (i > 0)
        {
            for (char& ch : word)
                ch = std::tolower(ch);
            result += ' ';
        }
        result += word;
    }
    return result;
}

int main()
{
    std::cout << parseFunction("public void testCowsCanBeMilked()") << "\n";
    std::cout << parseFunction("public void testSheepAreNotTheOnlyFruit()") << "\n";
    // Cows can be milked
    // Sheep are not the only fruit
}
```

I'm assuming the input is the full signature in the form shown (a return type, then the method name, then `()`), so the name is the token before the `(`. If only the bare method name got passed in, the name-extraction step just returns the whole thing and the rest works the same.

---

## Question 3: `#include` vs forward declaration

You only need the full `#include` when the compiler has to know widget's actual size or layout. If it only needs to know the name exists, a forward declaration (`class widget;`) is enough.

Only two of them actually need the include:

- #1 is the inheritance. fubar embeds widget as a base, so the compiler needs widget's full layout to build fubar.
- #11 is the by-value member. It lives inside every fubar, so fubar's size depends on widget's size, and for that you need the full type.

Everything else is fine with just a forward declaration:

- Anything that's a pointer or reference (params #3 #4 #6 #7, returns #9 #10, members #12 #13 #15 #16) is just an address, so the compiler never needs to look inside widget.
- The by-value params and returns (#2 #5 #8) are fine too. You only need the complete type where the function is actually defined or called, not where it's declared. `virtual` doesn't change that.

The sneaky one is #14, the static by-value member. It looks identical to #11 but doesn't need the include. A static member is only declared inside the class. Its real definition lives in the .cpp, and that's the file that actually needs the include. So you forward declare in the header and include in the .cpp.

fubar could depend on widget in other ways too, like calling its methods, accessing its members, constructing or destroying it, `delete`ing a `widget*` (delete needs the full type to run the destructor), taking its `sizeof`, or using it as a template argument. All of those need the complete type at the point of use.

In practice you forward declare in headers wherever you can, and push the real `#include` down into the .cpp. That stops a change to widget.h forcing half the codebase to recompile.

---

## Question 4: Code review, `FileStar`

The comments I'd feed back:

1. The filename is allocated with `strdup` but freed with `delete[]` in the destructor, which is undefined behaviour. `strdup` allocates with malloc, so it has to be freed with `free()`. Either call `free(filename)`, or better, store the name in a `std::string` and stop managing raw memory at all.
2. The destructor throws. If `fclose` fails it throws `FileStarError` from inside `~FileStar`, and if that happens while another exception is already propagating you've got two exceptions in flight and the program calls `std::terminate`. Destructors shouldn't throw, so log or swallow the close failure instead.
3. It breaks the Rule of Three. There's a destructor but no copy constructor or copy assignment, so if a `FileStar` gets copied the `filename` pointer and the `FILE*` are shallow-copied and two objects end up owning the same memory and the same file handle. When both are destroyed you get a double free and a double fclose. Either delete the copy operations, implement them properly, or use RAII members so the defaults are correct.
4. The `read()` check compares signed against unsigned. `fread` returns `size_t` but `size` is an `int`, so `fread(...) != size` converts the int to unsigned, and if `size` were ever negative it'd wrap to a huge value and the check would misbehave. Make `size` a `size_t`.

A couple of smaller things too. `what()` doesn't modify the object, so the method itself should be const-qualified, `const char *what() const`. The `const` that's already there only applies to the returned pointer, not to the method, so right now you couldn't call `what()` on a `const FileStarError`. And storing the message as a raw `const char*` is fine for the string literals being thrown here but would dangle if anyone ever built one from a temporary buffer.

Overall I'd ask for changes before approving. The cleanest fix is to lean on RAII, with a `std::string` for the filename and a self-closing wrapper (or a `unique_ptr<FILE, ...>` with a custom deleter) for the file. That makes the class Rule of Zero and kills the `delete[]` bug, the double free and the throwing destructor all at once.

---

## Question 5: Reacting immediately when pre-calc takes 100s of ms

The pre-calc can never run on the audio thread. It would blow the buffer deadline and you'd get a glitch. So the concepts are:

Do the heavy pre-calculation on a background worker thread, off the audio thread entirely. The audio thread just carries on making sound with its current settings the whole time that runs.

Then you hand the finished result over to the audio thread lock-free. Taking a lock on the audio thread is itself blocking, an unbounded wait with the risk of priority inversion, so you avoid it. That hand-over can be an atomic pointer swap, or an SPSC FIFO if you're streaming smaller messages. The worker builds the whole new thing off to the side and publishes it in one atomic operation, so the audio thread only ever sees the old complete result or the new complete one.

While the calc runs you bridge the gap. Keep using the old processor until the new one is ready, then crossfade over to it when it arrives so there's no click at the switchover.

The point is the audio thread is never waiting on anything. It's always running either the old thing or the freshly published new one.

I'm assuming a short latency before the new processing becomes audible is acceptable, as long as the audio itself never drops out.

The Question 7 program in [`src/q7_audio_engine.cpp`](src/q7_audio_engine.cpp) actually does this. It builds a new plugin chain on a worker thread, hands it over with a single atomic pointer swap, and crossfades it in while the audio keeps running, with the old chain deleted back on the main thread.

---

## Question 6: Measuring true (inter-sample) peak

Sample peak just looks at the actual sample values and takes the highest one. The catch is that the waveform leaving the DAC gets reconstructed between the samples, and that reconstructed curve can overshoot above the highest sample. Those overshoots are the inter-sample peaks. It means a signal that reads exactly 0 dBFS on its samples can still clip the converter.

To catch them you reconstruct what happens between the samples. You oversample the signal, typically by 4x, using interpolation or a low-pass filter to fill in the in-between values, then take the peak of that upsampled signal. That reveals the overshoots the raw samples were hiding.

This is basically what the ITU-R BS.1770 true peak measurement does. It oversamples by at least 4x and measures the max of the reconstructed signal. More oversampling is more accurate, but 4x is the standard practical amount.

So in short, sample peak measures the dots and true peak reconstructs the line between the dots and measures that, because the line is what actually hits the converter.

There's a runnable version in [`src/q6_true_peak.cpp`](src/q6_true_peak.cpp). It builds the worst-case signal (a tone at fs/4 whose samples all read 0 dBFS) and prints the 4x-oversampled true peak sitting 3 dB above them, which is exactly the overshoot the samples were hiding.

---

## Question 7: Architecture for device, mixer, plugins

I'd put an interface at each of the two points you want to swap out. One is the audio device, the other is the plugins. Concrete implementations then plug in without the engine or the mixer needing to know anything about them. It's the same thinking as dependency inversion, where the high-level parts depend on abstractions instead of on specific devices or plugins.

```
        +------------------+
        |   AudioEngine    |   owns + drives everything
        +------------------+
                 |
                 | talks to an interface, not a specific device
                 v
        +------------------+
        |   AudioDevice    |   interface: open / close / start / callback
        +------------------+
            ^          ^
            |          |        concrete backends implement it
   +----------------+ +----------------+
   | CoreAudioDevice| |   AsioDevice   |
   +----------------+ +----------------+

   audio from the device callback passes through:

        +------------------+
        |     Mixer        |   sums / routes channels
        +------------------+
                 | hosts a list of...
                 v
        +------------------+
        |      Plugin      |   interface: process(buffer) / setParam / prepare
        +------------------+
            ^          ^
            |          |        concrete plugins implement it
   +----------------+ +----------------+
   |  ReverbPlugin  | |    EqPlugin    |
   +----------------+ +----------------+
```

The AudioEngine grabs one of the installed devices through the `AudioDevice` interface, so it doesn't care which one it actually is. The device fires its callback on its own high-priority thread, the audio passes through the Mixer, and the Mixer runs its plugin chain by calling `process()` on each plugin through the common `Plugin` interface.

Having an interface in both spots means you can add a new device backend or a new plugin just by implementing it, without touching the engine or the mixer. It also lets you drop in a mock device or mock plugin when you want to test.

Everything inside the device callback (the mixer sums and every plugin's `process()`) has to be real-time safe though, so no allocating, no locking, no I/O. Plugin loading and any heavy setup happens off the audio thread and gets handed in, which ties back to Question Five.

This is built and runnable in [`src/q7_audio_engine.cpp`](src/q7_audio_engine.cpp). It wires an `AudioEngine` to a mock `AudioDevice` through the interface, runs a `Plugin` chain through the `Mixer`, and folds in the Question 5 hand-over, building a new chain off-thread and crossfading it in while the audio keeps running.

## License

[MIT](LICENSE).
