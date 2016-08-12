struct kevent {
    uintptr_t       ident;          /* identifier for this event */
    int16_t         filter;         /* filter for event */
    uint16_t        flags;          /* general flags */
    uint32_t        fflags;         /* filter-specific flags */
    intptr_t        data;           /* filter-specific data */
    void            *udata;         /* opaque user data identifier */
} kevent;

::kevent:entry
/pid == $target && arg3 != NULL/
{
    k = arg3;
}

::kevent:return
/pid == $target && k != NULL/
{
    x = (struct kevent *) copyin(k, sizeof(kevent));
    printf("FD: %d, Size: %d, Flags: %d, PTR: %d", x->ident, x->data, x->flags, x->filter);
    k = NULL;
}

/*
::kevent:entry
/pid == $target && arg1 != NULL/
{
    x = (struct kevent *) copyin(arg1, sizeof(kevent));
    printf("> Sock: %d, Flags: %d", x->ident, x->flags);
}
*/