# SYSC4001_A3P2
repository for SYSC4001 assignment 3 part 3.
SYSC4001 Assignment 3 - Part 2
Students: Matthew Gibeault and Declan Koster 
Student IDs: 1013237722 101301649

Overview:
This repository contains solutions for Part 2 of SYSC4001 Assignment 3:
part2a_1013237722_101301649.c: Concurrent processes using shared memory (race conditions allowed).
part2b_1013237722_101301649.c: Semaphore-based synchronization with shared memory.

Both programs simulate multiple Teaching Assistants (TAs) marking exams concurrently while sharing a rubric and a pile of exam files.

Compile Instructions:
Use gcc in Linux terminal:

# Part 2a
gcc part2a_1013237722_101301649.c -o part2a

# Part 2b (requires POSIX semaphores, link with pthread)
gcc part2b_1013237722_101301649.c -o part2b -pthread

Run Instructions:
The program requires:
Number of TAs (n ≥ 2)
Rubric file (e.g., rubric.txt)
At least 20 exam files (last file must contain student number 9999 to trigger termination)

Example run:
./part2a 3 rubric.txt exam1.txt exam2.txt exam3.txt exam4.txt exam5.txt exam6.txt exam7.txt exam8.txt exam9.txt exam10.txt exam11.txt exam12.txt exam13.txt exam14.txt exam15.txt exam16.txt exam17.txt exam18.txt exam19.txt exam20.txt


./part2b 3 rubric.txt exam1.txt exam2.txt exam3.txt exam4.txt exam5.txt exam6.txt exam7.txt exam8.txt exam9.txt exam10.txt exam11.txt exam12.txt exam13.txt exam14.txt exam15.txt exam16.txt exam17.txt exam18.txt exam19.txt exam20.txt


Expected Output:
BEFORE/AFTER logs for:
Reading and writing rubric lines
Marking questions
Loading exams into shared memory

Messages like:
[TA 1] Checking rubric...
[TA 1] BEFORE READ rubric[0] = '1, A'
[TA 1] AFTER WRITE rubric[1] change B -> C
[TA 3] Marked question 2 for student 1019
[COORD] Sentinel exam (9999) detected. Signaling termination.

All TAs terminate when student 9999 is reached.

Notes:
Part 2a allows race conditions (rubric edits and question marking may overlap).
Part 2b uses semaphores to enforce:
Mutual exclusion for rubric edits
Coordinated question marking
Controlled exam switching

No deadlock or livelock observed in tests; execution proceeds exam by exam until termination.
