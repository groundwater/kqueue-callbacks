syscall::accept:entry
/pid == $target/ 
{
    t = timestamp
}

syscall::accept:return
/pid == $target && t > 0/ 
{
    @avg[(timestamp - t)/1000] = count();
}
