#include "libc_backend.h"
#include "console.h"

int __libc_backend_outs( const char *s, size_t len )
{
  if(Console::stdout) 
    return Console::stdout->write(s,len);
  else
    return 1;
}

int __libc_backend_ins( char *s, size_t len )
{
  if(Console::stdin) {
    size_t act = 0;
    for(; act < len; act++) {
      s[act]=Console::stdin->getchar();
      if(s[act]=='\r') {
	act++;
	break;
      }
    }
    return act;
  } else
    return 0;
}
