#include "user.h"

void get_prime(int pipe_read, int pipe_write)
{
    // close the writing end of pipe in this process,
    // since if it's existing, read will not be closed
    close(pipe_write);

    // read from the fd provieded，output the first number,
    // if it's empty, indicating it has been done
    int print_prime;
    int state_of_pipe = read(pipe_read, &print_prime, sizeof(int));

    if (!state_of_pipe)
    {
        // pipe_read is useless in this case
        close(pipe_read);
        exit(0);
    }
    fprintf(1, "prime %d\n", print_prime);
    int pipe_prime;

    // create pipe
    int p1[2];
    pipe(p1);

    // create child process, set the pipe into params
    if (fork() == 0)
    {
        close(pipe_read);
        get_prime(p1[0], p1[1]);
    }
    else
    {
        close(p1[0]);
        // read from parent pipe and write to the next pipe
        while (read(pipe_read, &pipe_prime, sizeof(int)) > 0)
        {
            if (pipe_prime % print_prime != 0)
            {
                write(p1[1], &pipe_prime, sizeof(pipe_prime));
            }
        }
        close(pipe_read);
        close(p1[1]);

        // IMPORTANT: wait util the child process exits
        // if parent process didn't wait, child may exit before parent exist
        wait(0);
        exit(0);
    }
}

int main()
{
    int p[2];
    pipe(p);
    // put the initial numbers into the first pipe
    for (int i = 2; i <= 35; i++)
    {
        write(p[1], &i, sizeof(i));
    }
    // create entry to the recursion
    get_prime(p[0], p[1]);
    exit(0);
}
