From the start of my programming career, I’ve been interested in many different programming languages and technologies. BASIC was my first language, then Pascal, C, C++, and many other. I was fascinated how compilers work and one thing that got my attention the most was postfix languages like Forth. I loved my HP calculators that had a stack and postfix notation.

When I learned that Pascal many times is implemented using a stack virtual machine (SVM) and a byte code. It was something I wanted to do. I’ve done a bunch of smaller projects like this but never really completed them. I’d like to create a more complete project with a full language and full-featured VM. I also want it to support object oriented programming. So the plan is to develop a Pascal compiler as the fundamental platform to in turn develop a full featured stack VM.

The first level would be to aim for a Wirth compatible pascal syntax. The SVM does not have to be compatible with P-Code or any other VM, because I want to be in control in what direction it's going. Furthermore, I’d also would like to develop an “assembler” and “disassembler” for the byte code, and eventually even create a compiler for a BASIC dialect running on the VM.

Some other features I’d like to incorporate over time is GUI interface, sound, etc, i.e. essentially an application that can run in the GUI on Mac or Linux. Probably using GTK for that, but it’s a later task.

Also, I’d like to incorporate some database technology, like a smaller database support, ISAM files and such.

It should also support having pre-compiled library files that can be imported, which can create some challenges in itself because of jump addresses in the byte code.

All this is truly a large scale project, and I think the ultimate goal is to create a programming tool akin to d:Base or SQLWindows from the 80’s and 90’s. Something that eventually can run on multiple platforms and provide a simple way of creating personal applications for those who are interested.

I'm going to work this on a Mac, using C from the CLI. Later, I will start porting it to Linux.

