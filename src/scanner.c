/****************************************************************************
**
*W  scanner.c                   GAP source                   Martin Schönert
**
**
*Y  Copyright (C)  1996,  Lehrstuhl  für Mathematik,  RWTH Aachen,  Germany
*Y  (C) 1998 School Math and Comp. Sci., University of St Andrews, Scotland
*Y  Copyright (C) 2002 The GAP Group
**
**  This file contains the functions of the scanner, which is responsible for
**  all input and output processing.
**
**  The scanner  exports two very  important abstractions.  The  first is the
**  concept that an input file is  a stream of symbols,  such nasty things as
**  <space>,  <tab>,  <newline> characters or  comments (they are worst  :-),
**  characters making  up identifiers  or  digits that  make  up integers are
**  hidden from the rest of GAP.
**
**  The second is  the concept of  a current input  and output file.   In the
**  main   module   they are opened  and   closed  with the  'OpenInput'  and
**  'CloseInput' respectively  'OpenOutput' and 'CloseOutput' calls.  All the
**  other modules just read from the  current input  and write to the current
**  output file.
**
**  SL 5/99 I now plan to break the second abstraction in regard of output
**  streams. Instead of all Print/View/etc output going via Pr to PutLine, etc.
**  they will go via PrTo and PutLineTo. The extra argument of these will be
**  of type KOutputStream, a pointer to a C structure (using a GAP object would
**  be difficult in the early bootstrap, and because writing to a string stream
**  may cause a garbage collection, which can be a pain).
**
**  The scanner relies on the functions  provided  by  the  operating  system
**  dependent module 'system.c' for the low level input/output.
*/
#include        "system.h"              /* system dependent part           */


#include        "sysfiles.h"            /* file input/output               */

#include        "gasman.h"              /* garbage collector               */
#include        "objects.h"             /* objects                         */

#include        "scanner.h"             /* scanner                         */

#include        "code.h"                /* coder                           */

#include        "gap.h"                 /* error handling, initialisation  */

#include        "gvars.h"               /* global variables                */
#include        "calls.h"               /* generic call mechanism          */

#include        "bool.h"                /* booleans                        */

#include        "records.h"             /* generic records                 */
#include        "precord.h"             /* plain records                   */

#include        "lists.h"               /* generic lists                   */
#include        "plist.h"              /* plain lists                     */
#include        "stringobj.h"              /* strings                         */

#include        "opers.h"               /* DoFilter...                     */
#include        "read.h"                /* Call0ArgsInNewReader            */

#include	"hpc/tls.h"
#include	"hpc/thread.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

/* the following global variables are documented in scanner.h */

/* TL: UInt            Symbol; */

/* TL: Char            Value [1030]; */
/* TL: UInt            ValueLen; */

/* TL: UInt            NrError; */
/* TL: UInt            NrErrLine; */

/* TL: Char *          Prompt; */

Obj             PrintPromptHook = 0;
Obj             EndLineHook = 0;

/* TL: TypInputFile    InputFiles [16]; */
/* TL: TypInputFile *  Input; */
/* TL: Char *          In; */

/* TL: TypOutputFile   OutputFiles [16]; */
/* TL: TypOutputFile * Output; */

/* TL: TypOutputFile * InputLog; */

/* TL: TypOutputFile * OutputLog; */


/****************************************************************************
**
*V  TestInput . . . . . . . . . . . . .  file identifier of test input, local
*V  TestOutput  . . . . . . . . . . . . file identifier of test output, local
*V  TestLine  . . . . . . . . . . . . . . . . one line from test input, local
**
**  'TestInput' is the file identifier  of the file for  test input.  If this
**  is  not -1  and  'GetLine' reads  a line  from  'TestInput' that does not
**  begins with  'gap>'  'GetLine' assumes  that this was  expected as output
**  that did not appear and echoes this input line to 'TestOutput'.
**
**  'TestOutput' is the current output file  for test output.  If 'TestInput'
**  is not -1 then 'PutLine' compares every line that is  about to be printed
**  to 'TestOutput' with the next  line from 'TestInput'.   If this line does
**  not starts with 'gap>'  and the rest of  it  matches the output line  the
**  output  line  is not printed  and the  input   comment line is discarded.
**  Otherwise 'PutLine' prints the output line and does not discard the input
**  line.
**
**  'TestLine' holds the one line that is read from 'TestInput' to compare it
**  with a line that is about to be printed to 'TestOutput'.
*/
/* TL: TypInputFile *  TestInput  = 0; */
/* TL: TypOutputFile * TestOutput = 0; */
/* TL: Char            TestLine [256]; */


/****************************************************************************
**
*F  SyntaxError( <msg> )  . . . . . . . . . . . . . . .  raise a syntax error
**
*/
void            SyntaxError (
    const Char *        msg )
{
    Int                 i;

    /* open error output                                                   */
    OpenOutput( "*errout*" );
    assert(TLS(Output));

    /* one more error                                                      */
    TLS(NrError)++;
    TLS(NrErrLine)++;

    /* do not print a message if we found one already on the current line  */
    if ( TLS(NrErrLine) == 1 )

      {
        /* print the message and the filename, unless it is '*stdin*'          */
        Pr( "Syntax error: %s", (Int)msg, 0L );
        if ( strcmp( "*stdin*", TLS(Input)->name ) != 0 )
          Pr( " in %s:%d", (Int)TLS(Input)->name, (Int)TLS(Input)->number );
        Pr( "\n", 0L, 0L );

        /* print the current line                                              */
        Pr( "%s", (Int)TLS(Input)->line, 0L );

        /* print a '^' pointing to the current position                        */
        for ( i = 0; i < TLS(In) - TLS(Input)->line - 1; i++ ) {
          if ( TLS(Input)->line[i] == '\t' )  Pr("\t",0L,0L);
          else  Pr(" ",0L,0L);
        }
        Pr( "^\n", 0L, 0L );
      }
    /* close error output                                                  */
    assert(TLS(Output));
    CloseOutput();
    assert(TLS(Output));
}

/****************************************************************************
**
*F  SyntaxWarning( <msg> )  . . . . . . . . . . . . . . raise a syntax warning
**
*/
void            SyntaxWarning (
    const Char *        msg )
{
    Int                 i;

    /* open error output                                                   */
    OpenOutput( "*errout*" );
    assert(TLS(Output));


    /* do not print a message if we found one already on the current line  */
    if ( TLS(NrErrLine) == 0 )

      {
        /* print the message and the filename, unless it is '*stdin*'          */
        Pr( "Syntax warning: %s", (Int)msg, 0L );
        if ( strcmp( "*stdin*", TLS(Input)->name ) != 0 )
          Pr( " in %s:%d", (Int)TLS(Input)->name, (Int)TLS(Input)->number );
        Pr( "\n", 0L, 0L );

        /* print the current line                                              */
        Pr( "%s", (Int)TLS(Input)->line, 0L );

        /* print a '^' pointing to the current position                        */
        for ( i = 0; i < TLS(In) - TLS(Input)->line - 1; i++ ) {
          if ( TLS(Input)->line[i] == '\t' )  Pr("\t",0L,0L);
          else  Pr(" ",0L,0L);
        }
        Pr( "^\n", 0L, 0L );
      }
    /* close error output                                                  */
    assert(TLS(Output));
    CloseOutput();
    assert(TLS(Output));
}


/****************************************************************************
**
*F  Match( <symbol>, <msg>, <skipto> )  . match current symbol and fetch next
**
**  'Match' is the main  interface between the  scanner and the  parser.   It
**  performs the  4 most common actions in  the scanner  with  just one call.
**  First it checks that  the current symbol stored  in the variable 'Symbol'
**  is the expected symbol  as passed in the  argument <symbol>.  If  it  is,
**  'Match' reads the next symbol from input  and returns.  Otherwise 'Match'
**  first prints the current input line followed by the syntax error message:
**  '^ syntax error, <msg> expected' with '^' pointing to the current symbol.
**  It then  skips symbols up to one  in the resynchronisation  set <skipto>.
**  Actually 'Match' calls 'SyntaxError' so its comments apply here too.
**
**  One kind of typical 'Match' call has the form
**
**      'Match( Symbol, "", 0L );'.
**
**  This is used if the parser knows that the current  symbol is correct, for
**  example in 'RdReturn'  the   first symbol must be 'S_RETURN',   otherwise
**  'RdReturn' would not have been  called.  Called this  way 'Match' will of
**  course never raise an syntax error,  therefore <msg>  and <skipto> are of
**  no concern, they are passed nevertheless  to please  lint.  The effect of
**  this call is merely to read the next symbol from input.
**
**  Another typical 'Match' call is in 'RdIf' after we read the if symbol and
**  the condition following, and now expect to see the 'then' symbol:
**
**      Match( S_THEN, "then", STATBEGIN|S_ELIF|S_ELSE|S_FI|follow );
**
**  If the current symbol  is 'S_THEN' it is  matched  and the next symbol is
**  read.  Otherwise 'Match'  prints the  current line followed by the  error
**  message: '^ syntax error, then expected'.  Then 'Match' skips all symbols
**  until finding either  a symbol  that can begin  a statment,  an 'elif' or
**  'else' or 'fi' symbol, or a symbol that is  contained in the set <follow>
**  which is passed to  'RdIf' and contains  all symbols allowing  one of the
**  calling functions to resynchronize, for example 'S_OD' if 'RdIf' has been
**  called from 'RdFor'.  <follow>  always contain 'S_EOF', which 'Read' uses
**  to resynchronise.
**
**  If 'Match' needs to  read a  new line from  '*stdin*' or '*errin*' to get
**  the next symbol it prints the string pointed to by 'Prompt'.
*/
void Match (
    UInt                symbol,
    const Char *        msg,
    TypSymbolSet        skipto )
{
    Char                errmsg [256];

    /* if 'TLS(Symbol)' is the expected symbol match it away                    */
    if ( symbol == TLS(Symbol) ) {
        GetSymbol();
    }

    /* else generate an error message and skip to a symbol in <skipto>     */
    else {
        strlcpy( errmsg, msg, sizeof(errmsg) );
        strlcat( errmsg, " expected", sizeof(errmsg) );
        SyntaxError( errmsg );
        while ( ! IS_IN( TLS(Symbol), skipto ) )
            GetSymbol();
    }
}


/****************************************************************************
**

*F * * * * * * * * * * * open input/output functions  * * * * * * * * * * * *
*/


/****************************************************************************
**
*F  OpenInput( <filename> ) . . . . . . . . . .  open a file as current input
**
**  'OpenInput' opens  the file with  the name <filename>  as  current input.
**  All  subsequent input will  be taken from that  file, until it is  closed
**  again  with 'CloseInput'  or  another file  is opened  with  'OpenInput'.
**  'OpenInput'  will not  close the  current  file, i.e., if  <filename>  is
**  closed again, input will again be taken from the current input file.
**
**  'OpenInput'  returns 1 if  it   could  successfully open  <filename>  for
**  reading and 0  to indicate  failure.   'OpenInput' will fail if  the file
**  does not exist or if you do not have permissions to read it.  'OpenInput'
**  may  also fail if  you have too  many files open at once.   It  is system
**  dependent how many are  too many, but  16  files should  work everywhere.
**
**  Directely after the 'OpenInput' call the variable  'Symbol' has the value
**  'S_ILLEGAL' to indicate that no symbol has yet been  read from this file.
**  The first symbol is read by 'Read' in the first call to 'Match' call.
**
**  You can open  '*stdin*' to  read  from the standard  input file, which is
**  usually the terminal, or '*errin*' to  read from the standard error file,
**  which  is  the  terminal  even if '*stdin*'  is  redirected from  a file.
**  'OpenInput' passes those  file names  to  'SyFopen' like any other  name,
**  they are  just  a  convention between the  main  and the system  package.
**  'SyFopen' and thus 'OpenInput' will  fail to open  '*errin*' if the  file
**  'stderr'  (Unix file  descriptor  2)  is  not a  terminal,  because  of a
**  redirection say, to avoid that break loops take their input from a file.
**
**  It is not neccessary to open the initial input  file, 'InitScanner' opens
**  '*stdin*' for  that purpose.  This  file on   the other   hand  cannot be
**  closed by 'CloseInput'.
*/
UInt OpenInput (
    const Char *        filename )
{
    Int                 file;

    /* fail if we can not handle another open input file                   */
    if ( TLS(Input)+1 == TLS(InputFiles)+(sizeof(TLS(InputFiles))/sizeof(TLS(InputFiles)[0])) )
        return 0;

    /* in test mode keep reading from test input file for break loop input */
    if ( TLS(TestInput) != 0 && ! strcmp( filename, "*errin*" ) )
        return 1;

    /* try to open the input file                                          */
    file = SyFopen( filename, "r" );
    if ( file == -1 )
        return 0;

    /* remember the current position in the current file                   */
    if ( TLS(Input)+1 != TLS(InputFiles) ) {
        TLS(Input)->ptr    = TLS(In);
        TLS(Input)->symbol = TLS(Symbol);
    }

    /* enter the file identifier and the file name                         */
    TLS(Input)++;
    TLS(Input)->isstream = 0;
    TLS(Input)->file = file;
    if (strcmp("*errin*", filename) && strcmp("*stdin*", filename))
      TLS(Input)->echo = 0;
    else
      TLS(Input)->echo = 1;
    strlcpy( TLS(Input)->name, filename, sizeof(TLS(Input)->name) );
    TLS(Input)->gapname = (Obj) 0;

    /* start with an empty line and no symbol                              */
    TLS(In) = TLS(Input)->line;
    TLS(In)[0] = TLS(In)[1] = '\0';
    TLS(Symbol) = S_ILLEGAL;
    TLS(Input)->number = 1;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenInputStream( <stream> ) . . . . . . .  open a stream as current input
**
**  The same as 'OpenInput' but for streams.
*/
Obj IsStringStream;

UInt OpenInputStream (
    Obj                 stream )
{
    /* fail if we can not handle another open input file                   */
    if ( TLS(Input)+1 == TLS(InputFiles)+(sizeof(TLS(InputFiles))/sizeof(TLS(InputFiles)[0])) )
        return 0;

    /* remember the current position in the current file                   */
    if ( TLS(Input)+1 != TLS(InputFiles) ) {
        TLS(Input)->ptr    = TLS(In);
        TLS(Input)->symbol = TLS(Symbol);
    }

    /* enter the file identifier and the file name                         */
    TLS(Input)++;
    TLS(Input)->isstream = 1;
    TLS(Input)->stream = stream;
    TLS(Input)->isstringstream = (CALL_1ARGS(IsStringStream, stream) == True);
    if (TLS(Input)->isstringstream) {
        TLS(Input)->sline = ADDR_OBJ(stream)[2];
        TLS(Input)->spos = INT_INTOBJ(ADDR_OBJ(stream)[1]);
    }
    else {
        TLS(Input)->sline = 0;
    }
    TLS(Input)->file = -1;
    TLS(Input)->echo = 0;
    strlcpy( TLS(Input)->name, "stream", sizeof(TLS(Input)->name) );

    /* start with an empty line and no symbol                              */
    TLS(In) = TLS(Input)->line;
    TLS(In)[0] = TLS(In)[1] = '\0';
    TLS(Symbol) = S_ILLEGAL;
    TLS(Input)->number = 1;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  CloseInput()  . . . . . . . . . . . . . . . . .  close current input file
**
**  'CloseInput'  will close the  current input file.   Subsequent input will
**  again be taken from the previous input file.   'CloseInput' will return 1
**  to indicate success.
**
**  'CloseInput' will not close the initial input file '*stdin*', and returns
**  0  if such  an  attempt is made.   This is  used in  'Error'  which calls
**  'CloseInput' until it returns 0, therebye closing all open input files.
**
**  Calling 'CloseInput' if the  corresponding  'OpenInput' call failed  will
**  close the current output file, which will lead to very strange behaviour.
*/
UInt CloseInput ( void )
{
    /* refuse to close the initial input file                              */
    if ( TLS(Input) == TLS(InputFiles) )
        return 0;

    /* refuse to close the test input file                                 */
    if ( TLS(Input) == TLS(TestInput) )
        return 0;

    /* close the input file                                                */
    if ( ! TLS(Input)->isstream ) {
        SyFclose( TLS(Input)->file );
    }

    /* don't keep GAP objects alive unnecessarily */
    TLS(Input)->gapname = 0;
    TLS(Input)->sline = 0;

    /* revert to last file                                                 */
    TLS(Input)--;
    TLS(In)     = TLS(Input)->ptr;
    TLS(Symbol) = TLS(Input)->symbol;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  FlushRestOfInputLine()  . . . . . . . . . . . . discard remainder of line
*/

void FlushRestOfInputLine( void )
{
  TLS(In)[0] = TLS(In)[1] = '\0';
  /* TLS(Input)->number = 1; */
  TLS(Symbol) = S_ILLEGAL;
}



/****************************************************************************
**
*F  OpenTest( <filename> )  . . . . . . . .  open an input file for test mode
**
*/
UInt OpenTest (
    const Char *        filename )
{
    /* do not allow to nest test files                                     */
    if ( TLS(TestInput) != 0 )
        return 0;

    /* try to open the file as input file                                  */
    if ( ! OpenInput( filename ) )
        return 0;

    /* remember this is a test input                                       */
    TLS(TestInput)   = TLS(Input);
    TLS(TestOutput)  = TLS(Output);
    TLS(TestLine)[0] = '\0';

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenTestStream( <stream> )  . . . . .  open an input stream for test mode
**
*/
UInt OpenTestStream (
    Obj                 stream )
{
    /* do not allow to nest test files                                     */
    if ( TLS(TestInput) != 0 )
        return 0;

    /* try to open the file as input file                                  */
    if ( ! OpenInputStream( stream ) )
        return 0;

    /* remember this is a test input                                       */
    TLS(TestInput)   = TLS(Input);
    TLS(TestOutput)  = TLS(Output);
    TLS(TestLine)[0] = '\0';

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  CloseTest() . . . . . . . . . . . . . . . . . . close the test input file
**
*/
UInt CloseTest ( void )
{
    /* refuse to a non test file                                           */
    if ( TLS(TestInput) != TLS(Input) )
        return 0;

    /* close the input file                                                */
    if ( ! TLS(Input)->isstream ) {
        SyFclose( TLS(Input)->file );
    }

    /* don't keep GAP objects alive unnecessarily */
    TLS(Input)->gapname = 0;
    TLS(Input)->sline = 0;

    /* revert to last file                                                 */
    TLS(Input)--;
    TLS(In)     = TLS(Input)->ptr;
    TLS(Symbol) = TLS(Input)->symbol;

    /* we are no longer in test mode                                       */
    TLS(TestInput)   = 0;
    TLS(TestOutput)  = 0;
    TLS(TestLine)[0] = '\0';

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenLog( <filename> ) . . . . . . . . . . . . . log interaction to a file
**
**  'OpenLog'  instructs  the scanner to   echo  all  input   from  the files
**  '*stdin*' and  '*errin*'  and  all  output to  the  files '*stdout*'  and
**  '*errout*' to the file with  name <filename>.  The  file is truncated  to
**  size 0 if it existed, otherwise it is created.
**
**  'OpenLog' returns 1 if it could  successfully open <filename> for writing
**  and 0  to indicate failure.   'OpenLog' will  fail if  you do  not   have
**  permissions  to create the file or   write to  it.  'OpenOutput' may also
**  fail if you have too many files open at once.  It is system dependent how
**  many   are too   many, but  16   files should  work everywhere.   Finally
**  'OpenLog' will fail if there is already a current logfile.
*/
/* TL: static TypOutputFile LogFile; */

UInt OpenLog (
    const Char *        filename )
{

    /* refuse to open a logfile if we already log to one                   */
    if ( TLS(InputLog) != 0 || TLS(OutputLog) != 0 )
        return 0;

    /* try to open the file                                                */
    TLS(LogFile).file = SyFopen( filename, "w" );
    TLS(LogFile).isstream = 0;
    if ( TLS(LogFile).file == -1 )
        return 0;

    TLS(InputLog)  = &TLS(LogFile);
    TLS(OutputLog) = &TLS(LogFile);

    /* otherwise indicate success                                          */
    return 1;
}


/****************************************************************************
**
*F  OpenLogStream( <stream> ) . . . . . . . . . . log interaction to a stream
**
**  The same as 'OpenLog' but for streams.
*/
/* TL: static TypOutputFile LogStream; */

UInt OpenLogStream (
    Obj             stream )
{

    /* refuse to open a logfile if we already log to one                   */
    if ( TLS(InputLog) != 0 || TLS(OutputLog) != 0 )
        return 0;

    /* try to open the file                                                */
    TLS(LogStream).isstream = 1;
    TLS(LogStream).stream = stream;
    TLS(LogStream).file = -1;

    TLS(InputLog)  = &TLS(LogStream);
    TLS(OutputLog) = &TLS(LogStream);

    /* otherwise indicate success                                          */
    return 1;
}


/****************************************************************************
**
*F  CloseLog()  . . . . . . . . . . . . . . . . . . close the current logfile
**
**  'CloseLog' closes the current logfile again, so that input from '*stdin*'
**  and '*errin*' and output to '*stdout*' and '*errout*' will no  longer  be
**  echoed to a file.  'CloseLog' will return 1 to indicate success.
**
**  'CloseLog' will fail if there is no logfile active and will return  0  in
**  this case.
*/
UInt CloseLog ( void )
{
    /* refuse to close a non existent logfile                              */
    if ( TLS(InputLog) == 0 || TLS(OutputLog) == 0 || TLS(InputLog) != TLS(OutputLog) )
        return 0;

    /* close the logfile                                                   */
    if ( ! TLS(InputLog)->isstream ) {
        SyFclose( TLS(InputLog)->file );
    }
    TLS(InputLog)  = 0;
    TLS(OutputLog) = 0;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenInputLog( <filename> )  . . . . . . . . . . . . . log input to a file
**
**  'OpenInputLog'  instructs the  scanner  to echo  all input from the files
**  '*stdin*' and  '*errin*' to the file  with  name <filename>.  The file is
**  truncated to size 0 if it existed, otherwise it is created.
**
**  'OpenInputLog' returns 1  if it  could successfully open  <filename>  for
**  writing  and  0 to indicate failure.  'OpenInputLog' will fail  if you do
**  not have  permissions to create the file  or write to it.  'OpenInputLog'
**  may also fail  if you  have  too many  files open  at once.  It is system
**  dependent  how many are too many,  but 16 files  should work  everywhere.
**  Finally 'OpenInputLog' will fail if there is already a current logfile.
*/
/* TL: static TypOutputFile InputLogFile; */

UInt OpenInputLog (
    const Char *        filename )
{

    /* refuse to open a logfile if we already log to one                   */
    if ( TLS(InputLog) != 0 )
        return 0;

    /* try to open the file                                                */
    TLS(InputLogFile).file = SyFopen( filename, "w" );
    TLS(InputLogFile).isstream = 0;
    if ( TLS(InputLogFile).file == -1 )
        return 0;

    TLS(InputLog) = &TLS(InputLogFile);

    /* otherwise indicate success                                          */
    return 1;
}


/****************************************************************************
**
*F  OpenInputLogStream( <stream> )  . . . . . . . . . . log input to a stream
**
**  The same as 'OpenInputLog' but for streams.
*/
/* TL: static TypOutputFile InputLogStream; */

UInt OpenInputLogStream (
    Obj                 stream )
{

    /* refuse to open a logfile if we already log to one                   */
    if ( TLS(InputLog) != 0 )
        return 0;

    /* try to open the file                                                */
    TLS(InputLogStream).isstream = 1;
    TLS(InputLogStream).stream = stream;
    TLS(InputLogStream).file = -1;

    TLS(InputLog) = &TLS(InputLogStream);

    /* otherwise indicate success                                          */
    return 1;
}


/****************************************************************************
**
*F  CloseInputLog() . . . . . . . . . . . . . . . . close the current logfile
**
**  'CloseInputLog'  closes  the current  logfile again,  so  that input from
**  '*stdin*'  and   '*errin*'  will  no  longer   be  echoed   to  a   file.
**  'CloseInputLog' will return 1 to indicate success.
**
**  'CloseInputLog' will fail if there is no logfile active and will return 0
**  in this case.
*/
UInt CloseInputLog ( void )
{
    /* refuse to close a non existent logfile                              */
    if ( TLS(InputLog) == 0 )
        return 0;

    /* refuse to close a log opened with LogTo */
    if (TLS(InputLog) == TLS(OutputLog))
      return 0;
    
    /* close the logfile                                                   */
    if ( ! TLS(InputLog)->isstream ) {
        SyFclose( TLS(InputLog)->file );
    }

    TLS(InputLog) = 0;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenOutputLog( <filename> )  . . . . . . . . . . .  log output to a file
**
**  'OpenInputLog'  instructs the  scanner to echo   all output to  the files
**  '*stdout*' and '*errout*' to the file with name  <filename>.  The file is
**  truncated to size 0 if it existed, otherwise it is created.
**
**  'OpenOutputLog'  returns 1 if it  could  successfully open <filename> for
**  writing and 0 to  indicate failure.  'OpenOutputLog'  will fail if you do
**  not have permissions to create the file  or write to it.  'OpenOutputLog'
**  may also  fail if you have  too many  files  open at  once.  It is system
**  dependent how many are  too many,  but  16 files should  work everywhere.
**  Finally 'OpenOutputLog' will fail if there is already a current logfile.
*/
/* TL: static TypOutputFile OutputLogFile; */

UInt OpenOutputLog (
    const Char *        filename )
{

    /* refuse to open a logfile if we already log to one                   */
    if ( TLS(OutputLog) != 0 )
        return 0;

    /* try to open the file                                                */
    TLS(OutputLogFile).file = SyFopen( filename, "w" );
    TLS(OutputLogFile).isstream = 0;
    if ( TLS(OutputLogFile).file == -1 )
        return 0;

    TLS(OutputLog) = &TLS(OutputLogFile);

    /* otherwise indicate success                                          */
    return 1;
}


/****************************************************************************
**
*F  OpenOutputLogStream( <stream> )  . . . . . . . .  log output to a stream
**
**  The same as 'OpenOutputLog' but for streams.
*/
/* TL: static TypOutputFile outputLogStream; */

UInt OpenOutputLogStream (
    Obj                 stream )
{

    /* refuse to open a logfile if we already log to one                   */
    if ( TLS(OutputLog) != 0 )
        return 0;

    /* try to open the file                                                */
    TLS(OutputLogStream).isstream = 1;
    TLS(OutputLogStream).stream = stream;
    TLS(OutputLogStream).file = -1;

    TLS(OutputLog) = &TLS(OutputLogStream);

    /* otherwise indicate success                                          */
    return 1;
}


/****************************************************************************
**
*F  CloseOutputLog()  . . . . . . . . . . . . . . . close the current logfile
**
**  'CloseInputLog' closes   the current logfile   again, so  that output  to
**  '*stdout*'  and    '*errout*'  will no   longer  be   echoed to  a  file.
**  'CloseOutputLog' will return 1 to indicate success.
**
**  'CloseOutputLog' will fail if there is  no logfile active and will return
**  0 in this case.
*/
UInt CloseOutputLog ( void )
{
    /* refuse to close a non existent logfile                              */
    if ( TLS(OutputLog) == 0 )
        return 0;

    /* refuse to close a log opened with LogTo */
    if (TLS(OutputLog) == TLS(InputLog))
      return 0;

    /* close the logfile                                                   */
    if ( ! TLS(OutputLog)->isstream ) {
        SyFclose( TLS(OutputLog)->file );
    }

    TLS(OutputLog) = 0;

    /* indicate success                                                    */
    return 1;
}

/* TL: TypOutputFile*  IgnoreStdoutErrout = NULL; */

/****************************************************************************
**
*F  OpenOutput( <filename> )  . . . . . . . . . open a file as current output
**
**  'OpenOutput' opens the file  with the name  <filename> as current output.
**  All subsequent output will go  to that file, until either   it is  closed
**  again  with 'CloseOutput' or  another  file is  opened with 'OpenOutput'.
**  The file is truncated to size 0 if it existed, otherwise it  is  created.
**  'OpenOutput' does not  close  the  current file, i.e., if  <filename>  is
**  closed again, output will go again to the current output file.
**
**  'OpenOutput'  returns  1 if it  could  successfully  open  <filename> for
**  writing and 0 to indicate failure.  'OpenOutput' will fail if  you do not
**  have  permissions to create the  file or write   to it.  'OpenOutput' may
**  also   fail if you   have  too many files   open  at once.   It is system
**  dependent how many are too many, but 16 files should work everywhere.
**
**  You can open '*stdout*'  to write  to the standard output  file, which is
**  usually the terminal, or '*errout*' to write  to the standard error file,
**  which is the terminal  even   if '*stdout*'  is  redirected to   a  file.
**  'OpenOutput' passes  those  file names to 'SyFopen'  like any other name,
**  they are just a convention between the main and the system package.
**
**  It is not neccessary to open the initial output file, 'InitScanner' opens
**  '*stdout*' for that purpose.  This  file  on the other hand   can not  be
**  closed by 'CloseOutput'.
*/
UInt OpenOutput (
    const Char *        filename )
{
    Int                 file;

    /* do nothing for stdout and errout if catched */
    if ( TLS(Output) != NULL && TLS(IgnoreStdoutErrout) == TLS(Output) &&
          ( strcmp( filename, "*errout*" ) == 0
           || strcmp( filename, "*stdout*" ) == 0 ) ) {
        return 1;
    }

    /* fail if we can not handle another open output file                  */
    if ( TLS(Output)+1==TLS(OutputFiles)+(sizeof(TLS(OutputFiles))/sizeof(TLS(OutputFiles)[0])) )
        return 0;

    /* in test mode keep printing to test output file for breakloop output */
    if ( TLS(TestInput) != 0 && ! strcmp( filename, "*errout*" ) )
        return 1;

    /* try to open the file                                                */
    file = SyFopen( filename, "w" );
    if ( file == -1 )
        return 0;

    /* put the file on the stack, start at position 0 on an empty line     */
    if (TLS(Output) == 0L)
      TLS(Output) = TLS(OutputFiles);
    else
      TLS(Output)++;
    TLS(Output)->file     = file;
    TLS(Output)->line[0]  = '\0';
    TLS(Output)->pos      = 0;
    TLS(Output)->indent   = 0;
    TLS(Output)->isstream = 0;
    TLS(Output)->format   = 1;

    /* variables related to line splitting, very bad place to split        */
    TLS(Output)->hints[0] = -1;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenOutputStream( <stream> )  . . . . . . open a stream as current output
**
**  The same as 'OpenOutput' but for streams.
*/

Obj PrintFormattingStatus;

UInt OpenOutputStream (
    Obj                 stream )
{
    /* fail if we can not handle another open output file                  */
    if ( TLS(Output)+1==TLS(OutputFiles)+(sizeof(TLS(OutputFiles))/sizeof(TLS(OutputFiles)[0])) )
        return 0;

    /* put the file on the stack, start at position 0 on an empty line     */
    TLS(Output)++;
    TLS(Output)->stream   = stream;
    TLS(Output)->isstringstream = (CALL_1ARGS(IsStringStream, stream) == True);
    TLS(Output)->format   = (CALL_1ARGS(PrintFormattingStatus, stream) == True);
    TLS(Output)->line[0]  = '\0';
    TLS(Output)->pos      = 0;
    TLS(Output)->indent   = 0;
    TLS(Output)->isstream = 1;

    /* variables related to line splitting, very bad place to split        */
    TLS(Output)->hints[0] = -1;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  CloseOutput() . . . . . . . . . . . . . . . . . close current output file
**
**  'CloseOutput' will  first flush all   pending output and  then  close the
**  current  output  file.   Subsequent output will  again go to the previous
**  output file.  'CloseOutput' returns 1 to indicate success.
**
**  'CloseOutput' will  not  close the  initial output file   '*stdout*', and
**  returns 0 if such attempt is made.  This  is  used in 'Error' which calls
**  'CloseOutput' until it returns 0, thereby closing all open output files.
**
**  Calling 'CloseOutput' if the corresponding 'OpenOutput' call failed  will
**  close the current output file, which will lead to very strange behaviour.
**  On the other  hand if you  forget  to call  'CloseOutput' at the end of a
**  'PrintTo' call or an error will not yield much better results.
*/
UInt CloseOutput ( void )
{
    /* silently refuse to close the test output file this is probably
         an attempt to close *errout* which is silently not opened, so
         lets silently not close it  */
    if ( TLS(Output) == TLS(TestOutput) )
        return 1;
    /* and similarly */
    if ( TLS(IgnoreStdoutErrout) == TLS(Output) )
        return 1;

    /* refuse to close the initial output file '*stdout*'                  */
    if ( TLS(Output) == TLS(OutputFiles) )
        return 0;

    /* flush output and close the file                                     */
    Pr( "%c", (Int)'\03', 0L );
    if ( ! TLS(Output)->isstream ) {
        SyFclose( TLS(Output)->file );
    }

    /* revert to previous output file and indicate success                 */
    TLS(Output)--;
    return 1;
}


/****************************************************************************
**
*F  OpenAppend( <filename> )  . . open a file as current output for appending
**
**  'OpenAppend' opens the file  with the name  <filename> as current output.
**  All subsequent output will go  to that file, until either   it is  closed
**  again  with 'CloseOutput' or  another  file is  opened with 'OpenOutput'.
**  Unlike 'OpenOutput' 'OpenAppend' does not truncate the file to size 0  if
**  it exists.  Appart from that 'OpenAppend' is equal to 'OpenOutput' so its
**  description applies to 'OpenAppend' too.
*/
UInt OpenAppend (
    const Char *        filename )
{
    Int                 file;

    /* fail if we can not handle another open output file                  */
    if ( TLS(Output)+1==TLS(OutputFiles)+(sizeof(TLS(OutputFiles))/sizeof(TLS(OutputFiles)[0])) )
        return 0;

    /* in test mode keep printing to test output file for breakloop output */
    if ( TLS(TestInput) != 0 && ! strcmp( filename, "*errout*" ) )
        return 1;

    /* try to open the file                                                */
    file = SyFopen( filename, "a" );
    if ( file == -1 )
        return 0;

    /* put the file on the stack, start at position 0 on an empty line     */
    TLS(Output)++;
    TLS(Output)->file     = file;
    TLS(Output)->line[0]  = '\0';
    TLS(Output)->pos      = 0;
    TLS(Output)->indent   = 0;
    TLS(Output)->isstream = 0;

    /* variables related to line splitting, very bad place to split        */
    TLS(Output)->hints[0] = -1;

    /* indicate success                                                    */
    return 1;
}


/****************************************************************************
**
*F  OpenAppendStream( <stream> )  . . . . . . open a stream as current output
**
**  The same as 'OpenAppend' but for streams.
*/
UInt OpenAppendStream (
    Obj                 stream )
{
    return OpenOutputStream(stream);
}


/****************************************************************************
**

*F * * * * * * * * * * * * * * input functions  * * * * * * * * * * * * * * *
*/


/****************************************************************************
**

*V  ReadLineFunc  . . . . . . . . . . . . . . . . . . . . . . . .  'ReadLine'
*/
Obj ReadLineFunc;


/****************************************************************************
**
*F  GetLine2( <input>, <buffer>, <length> ) . . . . . . . . get a line, local
*/
static Int GetLine2 (
    TypInputFile *          input,
    Char *                  buffer,
    UInt                    length )
{

    if ( input->isstream ) {
        if ( input->sline == 0
          || GET_LEN_STRING(input->sline) <= input->spos )
        {
            input->sline = CALL_1ARGS( ReadLineFunc, input->stream );
            input->spos  = 0;
        }
        if ( input->sline == Fail || ! IS_STRING(input->sline) ) {
            return 0;
        }
        else {
            ConvString(input->sline);
            /* we now allow that input->sline actually contains several lines,
               e.g., it can be a  string from a string stream  */
            {
                /***  probably this can be a bit more optimized  ***/
                register Char * ptr, * bptr;
                register UInt count, len, max, cbuf;
                /* start position in buffer */
                for(cbuf = 0; buffer[cbuf]; cbuf++);
                /* copy piece of input->sline into buffer and adjust counters */
                for(count = input->spos,
                    ptr = (Char *)CHARS_STRING(input->sline) + count,
                    len = GET_LEN_STRING(input->sline),
                    max = length-2,
                    bptr = buffer + cbuf;
                    cbuf < max && count < len
                                  && *ptr != '\n' && *ptr != '\r';
                    *bptr = *ptr, cbuf++, ptr++, bptr++, count++);
                /* we also copy an end of line if there is one */
                if (*ptr == '\n' || *ptr == '\r') {
                    buffer[cbuf] = *ptr;
                    cbuf++;
                    count++;
                }
                buffer[cbuf] = '\0';
                input->spos = count;
                /* if input->stream is a string stream, we have to adjust the
                   position counter in the stream object as well */
                if (input->isstringstream) {
                    ADDR_OBJ(input->stream)[1] = INTOBJ_INT(count);
                }
            }
        }
    }
    else {
        if ( ! SyFgets( buffer, length, input->file ) ) {
            return 0;
        }
    }
    return 1;
}


/****************************************************************************
**
*F  GetLine() . . . . . . . . . . . . . . . . . . . . . . . get a line, local
**
**  'GetLine'  fetches another  line from  the  input 'Input' into the buffer
**  'Input->line', sets the pointer 'In' to  the beginning of this buffer and
**  returns the first character from the line.
**
**  If   the input file is  '*stdin*'   or '*errin*' 'GetLine'  first  prints
**  'Prompt', unless it is '*stdin*' and GAP was called with option '-q'.
**
**  If there is an  input logfile in use  and the input  file is '*stdin*' or
**  '*errin*' 'GetLine' echoes the new line to the logfile.
*/
extern void PutLine2(
    TypOutputFile *         output,
    const Char *            line,
    UInt                    len   );

/* TL: Int HELPSubsOn = 1; */

Char GetLine ( void )
{
    Char            buf[200];
    Char *          p;
    Char *          q;

    /* if file is '*stdin*' or '*errin*' print the prompt and flush it     */
    /* if the GAP function `PrintPromptHook' is defined then it is called  */
    /* for printing the prompt, see also `EndLineHook'                     */
    if ( ! TLS(Input)->isstream ) {
       if ( TLS(Input)->file == 0 ) {
            if ( ! SyQuiet ) {
                if (TLS(Output)->pos > 0)
                    Pr("\n", 0L, 0L);
                if ( PrintPromptHook )
                     Call0ArgsInNewReader( PrintPromptHook );
                else
                     Pr( "%s%c", (Int)TLS(Prompt), (Int)'\03' );
            } else
                Pr( "%c", (Int)'\03', 0L );
        }
        else if ( TLS(Input)->file == 2 ) {
            if (TLS(Output)->pos > 0)
                Pr("\n", 0L, 0L);
            if ( PrintPromptHook )
                 Call0ArgsInNewReader( PrintPromptHook );
            else
                 Pr( "%s%c", (Int)TLS(Prompt), (Int)'\03' );
        }
    }

    /* bump the line number                                                */
    if ( TLS(Input)->line < TLS(In) && (*(TLS(In)-1) == '\n' || *(TLS(In)-1) == '\r') ) {
        TLS(Input)->number++;
    }

    /* initialize 'TLS(In)', no errors on this line so far                      */
    TLS(In) = TLS(Input)->line;  TLS(In)[0] = '\0';
    TLS(NrErrLine) = 0;

    /* read a line from an ordinary input file                             */
    if ( TLS(TestInput) != TLS(Input) ) {

        /* try to read a line                                              */
        if ( ! GetLine2( TLS(Input), TLS(Input)->line, sizeof(TLS(Input)->line) ) ) {
            TLS(In)[0] = '\377';  TLS(In)[1] = '\0';
        }


        /* convert '?' at the beginning into 'HELP'
           (if not inside reading long string which may have line
           or chunk from GetLine starting with '?')                        */

        if ( TLS(In)[0] == '?' && TLS(HELPSubsOn) == 1) {
            strlcpy( buf, TLS(In)+1, sizeof(buf) );
            strcpy( TLS(In), "HELP(\"" );
            for ( p = TLS(In)+6,  q = buf;  *q;  q++ ) {
                if ( *q != '"' && *q != '\n' ) {
                    *p++ = *q;
                }
                else if ( *q == '"' ) {
                    *p++ = '\\';
                    *p++ = *q;
                }
            }
            *p = '\0';
            /* FIXME: We should do bounds checking, but don't know what 'In' points to */
            strcat( TLS(In), "\");\n" );
        }

        /* if necessary echo the line to the logfile                      */
        if( TLS(InputLog) != 0 && TLS(Input)->echo == 1)
            if ( !(TLS(In)[0] == '\377' && TLS(In)[1] == '\0') )
            PutLine2( TLS(InputLog), TLS(In), strlen(TLS(In)) );

                /*      if ( ! TLS(Input)->isstream ) {
          if ( TLS(InputLog) != 0 && ! TLS(Input)->isstream ) {
            if ( TLS(Input)->file == 0 || TLS(Input)->file == 2 ) {
              PutLine2( TLS(InputLog), TLS(In) );
            }
            }
            } */

    }

    /* read a line for test input file                                     */
    else {

        /* continue until we got an input line                             */
        while ( TLS(In)[0] == '\0' ) {

            /* there may be one line waiting                               */
            if ( TLS(TestLine)[0] != '\0' ) {
                strncat( TLS(In), TLS(TestLine), sizeof(TLS(Input)->line) );
                TLS(TestLine)[0] = '\0';
            }

            /* otherwise try to read a line                                */
            else {
                if ( ! GetLine2(TLS(Input), TLS(Input)->line, sizeof(TLS(Input)->line)) ) {
                    TLS(In)[0] = '\377';  TLS(In)[1] = '\0';
                }
            }

            /* if the line starts with a prompt its an input line          */
            if      ( TLS(In)[0] == 'g' && TLS(In)[1] == 'a' && TLS(In)[2] == 'p'
                   && TLS(In)[3] == '>' && TLS(In)[4] == ' ' ) {
                TLS(In) = TLS(In) + 5;
            }
            else if ( TLS(In)[0] == '>' && TLS(In)[1] == ' ' ) {
                TLS(In) = TLS(In) + 2;
            }

            /* if the line is not empty or a comment, print it             */
            else if ( TLS(In)[0] != '\n' && TLS(In)[0] != '#' && TLS(In)[0] != '\377' ) {
                /* Commented out by AK
                char obuf[8];
                snprintf(obuf, sizeof(obuf), "-%5i:\n- ", (int)TLS(TestInput)->number++);
                PutLine2( TLS(TestOutput), obuf, 7 );
                */
                PutLine2( TLS(TestOutput), "- ", 2 );
                PutLine2( TLS(TestOutput), TLS(In), strlen(TLS(In)) );
                TLS(In)[0] = '\0';
            }

        }

    }

    /* return the current character                                        */
    return *TLS(In);
}


/****************************************************************************
**
*F  GET_CHAR()  . . . . . . . . . . . . . . . . get the next character, local
**
**  'GET_CHAR' returns the next character from  the current input file.  This
**  character is afterwords also available as '*In'.
**
**  For efficiency  reasons 'GET_CHAR' is a  macro that  just  increments the
**  pointer 'In'  and checks that  there is another  character.  If  not, for
**  example at the end a line, 'GET_CHAR' calls 'GetLine' to fetch a new line
**  from the input file.
*/

static Char Pushback = '\0';
static Char *RealIn;

static inline void GET_CHAR( void ) {
  if (TLS(In) == &Pushback) {
      TLS(In) = RealIn;
  } else
    TLS(In)++;
  if (!*TLS(In))
    GetLine();
}

static inline void UNGET_CHAR( Char c ) {
  assert(TLS(In) != &Pushback);
  Pushback = c;
  RealIn = TLS(In);
  TLS(In) = &Pushback;
}


/****************************************************************************
**
*F  GetIdent()  . . . . . . . . . . . . . get an identifier or keyword, local
**
**  'GetIdent' reads   an identifier from  the current  input  file  into the
**  variable 'TLS(Value)' and sets 'Symbol' to 'S_IDENT'.   The first character of
**  the   identifier  is  the current character  pointed to  by 'In'.  If the
**  characters make  up   a  keyword 'GetIdent'  will  set   'Symbol'  to the
**  corresponding value.  The parser will ignore 'TLS(Value)' in this case.
**
**  An  identifier consists of a letter  followed by more letters, digits and
**  underscores '_'.  An identifier is terminated by the first  character not
**  in this  class.  The escape sequence '\<newline>'  is ignored,  making it
**  possible to split  long identifiers  over multiple lines.  The  backslash
**  '\' can be used  to include special characters like  '('  in identifiers.
**  For example 'G\(2\,5\)' is an identifier not a call to a function 'G'.
**
**  The size  of 'TLS(Value)' limits the  number  of significant characters  in an
**  identifier.   If  an  identifier   has more characters    'GetIdent' will
**  silently truncate it.
**
**  After reading the identifier 'GetIdent'  looks at the  first and the last
**  character  of  'TLS(Value)' to see if  it  could possibly  be  a keyword.  For
**  example 'test'  could  not be  a  keyword  because there  is  no  keyword
**  starting and ending with a 't'.  After that  test either 'GetIdent' knows
**  that 'TLS(Value)' is not a keyword, or there is a unique possible keyword that
**  could match, because   no two  keywords  have  identical  first and  last
**  characters.  For example if 'TLS(Value)' starts with 'f' and ends with 'n' the
**  only possible keyword  is 'function'.   Thus in this case  'GetIdent' can
**  decide with one string comparison if 'TLS(Value)' holds a keyword or not.
*/
extern void GetSymbol ( void );

typedef struct {const Char *name; UInt sym;} s_keyword;

static const s_keyword AllKeywords[] = {
  {"and",       S_AND},
  {"atomic",    S_ATOMIC},
  {"break",     S_BREAK},
  {"continue",  S_CONTINUE},
  {"do",        S_DO},
  {"elif",      S_ELIF},
  {"else",      S_ELSE},
  {"end",       S_END},
  {"false",     S_FALSE},
  {"fi",        S_FI},
  {"for",       S_FOR},
  {"function",  S_FUNCTION},
  {"if",        S_IF},
  {"in",        S_IN},
  {"local",     S_LOCAL},
  {"mod",       S_MOD},
  {"not",       S_NOT},
  {"od",        S_OD},
  {"or",        S_OR},
  {"readonly",  S_READONLY},
  {"readwrite", S_READWRITE},
  {"rec",       S_REC},
  {"repeat",    S_REPEAT},
  {"return",    S_RETURN},
  {"then",      S_THEN},
  {"true",      S_TRUE},
  {"until",     S_UNTIL},
  {"while",     S_WHILE},
  {"quit",      S_QUIT},
  {"QUIT",      S_QQUIT},
  {"IsBound",   S_ISBOUND},
  {"Unbind",    S_UNBIND},
  {"TryNextMethod", S_TRYNEXT},
  {"Info",      S_INFO},
  {"Assert",    S_ASSERT}};


static int IsIdent(char c) {
    return IsAlpha(c) || c == '_' || c == '@';
}

void GetIdent ( void )
{
    Int                 i, fetch;
    Int                 isQuoted;

    /* initially it could be a keyword                                     */
    isQuoted = 0;

    /* read all characters into 'TLS(Value)'                                    */
    for ( i=0; IsIdent(*TLS(In)) || IsDigit(*TLS(In)) || *TLS(In)=='\\'; i++ ) {

        fetch = 1;
        /* handle escape sequences                                         */
        /* we ignore '\ newline' by decrementing i, except at the
           very start of the identifier, when we cannot do that
           so we recurse instead                                           */
        if ( *TLS(In) == '\\' ) {
            GET_CHAR();
            if      ( *TLS(In) == '\n' && i == 0 )  { GetSymbol();  return; }
            else if ( *TLS(In) == '\r' )  {
                GET_CHAR();
                if  ( *TLS(In) == '\n' )  {
                     if (i == 0) { GetSymbol();  return; }
                     else i--;
                }
                else  {TLS(Value)[i] = '\r'; fetch = 0;}
            }
            else if ( *TLS(In) == '\n' && i < SAFE_VALUE_SIZE-1 )  i--;
            else if ( *TLS(In) == 'n'  && i < SAFE_VALUE_SIZE-1 )  TLS(Value)[i] = '\n';
            else if ( *TLS(In) == 't'  && i < SAFE_VALUE_SIZE-1 )  TLS(Value)[i] = '\t';
            else if ( *TLS(In) == 'r'  && i < SAFE_VALUE_SIZE-1 )  TLS(Value)[i] = '\r';
            else if ( *TLS(In) == 'b'  && i < SAFE_VALUE_SIZE-1 )  TLS(Value)[i] = '\b';
            else if ( i < SAFE_VALUE_SIZE-1 )  {
                TLS(Value)[i] = *TLS(In);
                isQuoted = 1;
            }
        }

        /* put normal chars into 'TLS(Value)' but only if there is room         */
        else {
            if ( i < SAFE_VALUE_SIZE-1 )  TLS(Value)[i] = *TLS(In);
        }

        /* read the next character                                         */
        if (fetch) GET_CHAR();

    }

    /* terminate the identifier and lets assume that it is not a keyword   */
    if ( i < SAFE_VALUE_SIZE-1 )
        TLS(Value)[i] = '\0';
    else {
        SyntaxError("Identifiers in GAP must consist of less than 1023 characters.");
        i =  SAFE_VALUE_SIZE-1;
        TLS(Value)[i] = '\0';
    }
    TLS(Symbol) = S_IDENT;

    /* now check if 'TLS(Value)' holds a keyword                                */
    switch ( 256*TLS(Value)[0]+TLS(Value)[i-1] ) {
    case 256*'a'+'d': if(!strcmp(TLS(Value),"and"))     TLS(Symbol)=S_AND;     break;
    case 256*'a'+'c': if(!strcmp(TLS(Value),"atomic"))  TLS(Symbol)=S_ATOMIC;  break;
    case 256*'b'+'k': if(!strcmp(TLS(Value),"break"))   TLS(Symbol)=S_BREAK;   break;
    case 256*'c'+'e': if(!strcmp(TLS(Value),"continue"))   TLS(Symbol)=S_CONTINUE;   break;
    case 256*'d'+'o': if(!strcmp(TLS(Value),"do"))      TLS(Symbol)=S_DO;      break;
    case 256*'e'+'f': if(!strcmp(TLS(Value),"elif"))    TLS(Symbol)=S_ELIF;    break;
    case 256*'e'+'e': if(!strcmp(TLS(Value),"else"))    TLS(Symbol)=S_ELSE;    break;
    case 256*'e'+'d': if(!strcmp(TLS(Value),"end"))     TLS(Symbol)=S_END;     break;
    case 256*'f'+'e': if(!strcmp(TLS(Value),"false"))   TLS(Symbol)=S_FALSE;   break;
    case 256*'f'+'i': if(!strcmp(TLS(Value),"fi"))      TLS(Symbol)=S_FI;      break;
    case 256*'f'+'r': if(!strcmp(TLS(Value),"for"))     TLS(Symbol)=S_FOR;     break;
    case 256*'f'+'n': if(!strcmp(TLS(Value),"function"))TLS(Symbol)=S_FUNCTION;break;
    case 256*'i'+'f': if(!strcmp(TLS(Value),"if"))      TLS(Symbol)=S_IF;      break;
    case 256*'i'+'n': if(!strcmp(TLS(Value),"in"))      TLS(Symbol)=S_IN;      break;
    case 256*'l'+'l': if(!strcmp(TLS(Value),"local"))   TLS(Symbol)=S_LOCAL;   break;
    case 256*'m'+'d': if(!strcmp(TLS(Value),"mod"))     TLS(Symbol)=S_MOD;     break;
    case 256*'n'+'t': if(!strcmp(TLS(Value),"not"))     TLS(Symbol)=S_NOT;     break;
    case 256*'o'+'d': if(!strcmp(TLS(Value),"od"))      TLS(Symbol)=S_OD;      break;
    case 256*'o'+'r': if(!strcmp(TLS(Value),"or"))      TLS(Symbol)=S_OR;      break;
    case 256*'r'+'e': if(!strcmp(TLS(Value),"readwrite")) TLS(Symbol)=S_READWRITE;     break;
    case 256*'r'+'y': if(!strcmp(TLS(Value),"readonly"))  TLS(Symbol)=S_READONLY;     break;
    case 256*'r'+'c': if(!strcmp(TLS(Value),"rec"))     TLS(Symbol)=S_REC;     break;
    case 256*'r'+'t': if(!strcmp(TLS(Value),"repeat"))  TLS(Symbol)=S_REPEAT;  break;
    case 256*'r'+'n': if(!strcmp(TLS(Value),"return"))  TLS(Symbol)=S_RETURN;  break;
    case 256*'t'+'n': if(!strcmp(TLS(Value),"then"))    TLS(Symbol)=S_THEN;    break;
    case 256*'t'+'e': if(!strcmp(TLS(Value),"true"))    TLS(Symbol)=S_TRUE;    break;
    case 256*'u'+'l': if(!strcmp(TLS(Value),"until"))   TLS(Symbol)=S_UNTIL;   break;
    case 256*'w'+'e': if(!strcmp(TLS(Value),"while"))   TLS(Symbol)=S_WHILE;   break;
    case 256*'q'+'t': if(!strcmp(TLS(Value),"quit"))    TLS(Symbol)=S_QUIT;    break;
    case 256*'Q'+'T': if(!strcmp(TLS(Value),"QUIT"))    TLS(Symbol)=S_QQUIT;   break;

    case 256*'I'+'d': if(!strcmp(TLS(Value),"IsBound")) TLS(Symbol)=S_ISBOUND; break;
    case 256*'U'+'d': if(!strcmp(TLS(Value),"Unbind"))  TLS(Symbol)=S_UNBIND;  break;
    case 256*'T'+'d': if(!strcmp(TLS(Value),"TryNextMethod"))
                                                     TLS(Symbol)=S_TRYNEXT; break;
    case 256*'I'+'o': if(!strcmp(TLS(Value),"Info"))    TLS(Symbol)=S_INFO;    break;
    case 256*'A'+'t': if(!strcmp(TLS(Value),"Assert"))  TLS(Symbol)=S_ASSERT;  break;

    default: ;
    }

    /* if it is quoted it is an identifier                                 */
    if ( isQuoted )  TLS(Symbol) = S_IDENT;


}


/******************************************************************************
*F  GetNumber()  . . . . . . . . . . . . . .  get an integer or float literal
**
**  'GetNumber' reads  a number from  the  current  input file into the
**  variable  'TLS(Value)' and sets  'Symbol' to 'S_INT', 'S_PARTIALINT',
**  'S_FLOAT' or 'S_PARTIALFLOAT'.   The first character of
**  the number is the current character pointed to by 'In'.
**
**  If the sequence contains characters which do not match the regular expression
**  [0-9]+.?[0-9]*([edqEDQ][+-]?[0-9]+)? 'GetNumber'  will
**  interpret the sequence as an identifier and set 'Symbol' to 'S_IDENT'.
**
**  As we read, we keep track of whether we have seen a . or exponent notation
**  and so whether we will return S_[PARTIAL]INT or S_[PARTIAL]FLOAT.
**
**  When TLS(Value) is  completely filled we have to check  if the reading of
**  the number  is complete  or not to  decide whether to return a PARTIAL type.
**
**  The argument reflects how far we are through reading a possibly very long number
**  literal. 0 indicates that nothing has been read. 1 that at least one digit has been
**  read, but no decimal point. 2 that a decimal point has been read with no digits before
**  or after it. 3 a decimal point and at least one digit, but no exponential indicator
**  4 an exponential indicator  but no exponent digits and 5 an exponential indicator and
**  at least one exponent digit.
**
*/
static Char GetCleanedChar( UInt *wasEscaped ) {
  GET_CHAR();
  *wasEscaped = 0;
  if (*TLS(In) == '\\') {
    GET_CHAR();
    if      ( *TLS(In) == '\n')
      return GetCleanedChar(wasEscaped);
    else if ( *TLS(In) == '\r' )  {
      GET_CHAR();
      if  ( *TLS(In) == '\n' )
        return GetCleanedChar(wasEscaped);
      else {
        UNGET_CHAR(*TLS(In));
        *wasEscaped = 1;
        return '\r';
      }
    }
    else {
      *wasEscaped = 1;
      if ( *TLS(In) == 'n')  return '\n';
      else if ( *TLS(In) == 't')  return '\t';
      else if ( *TLS(In) == 'r')  return '\r';
      else if ( *TLS(In) == 'b')  return '\b';
      else if ( *TLS(In) == '>')  return '\01';
      else if ( *TLS(In) == '<')  return '\02';
      else if ( *TLS(In) == 'c')  return '\03';
    }
  }
  return *TLS(In);
}


void GetNumber ( UInt StartingStatus )
{
  Int                 i=0;
  Char                c;
  UInt seenExp = 0;
  UInt wasEscaped = 0;
  UInt seenADigit = (StartingStatus != 0 && StartingStatus != 2);
  UInt seenExpDigit = (StartingStatus ==5);

  c = *TLS(In);
  if (StartingStatus  <  2) {
    /* read initial sequence of digits into 'Value'             */
    for (i = 0; !wasEscaped && IsDigit(c) && i < SAFE_VALUE_SIZE-1; i++) {
      TLS(Value)[i] = c;
      seenADigit = 1;
      c = GetCleanedChar(&wasEscaped);
    }

    /* So why did we run off the end of that loop */
    /* maybe we saw an identifier character and realised that this is an identifier we are reading */
    if (wasEscaped || IsIdent(c)) {
      /* Now we know we have an identifier read the rest of it */
      TLS(Value)[i++] = c;
      c = GetCleanedChar(&wasEscaped);
      for (; wasEscaped || IsIdent(c) || IsDigit(c); i++) {
        if (i < SAFE_VALUE_SIZE -1)
          TLS(Value)[i] = c;
        c = GetCleanedChar(&wasEscaped);
      }
      if (i < SAFE_VALUE_SIZE -1)
        TLS(Value)[i] = '\0';
      else
        TLS(Value)[SAFE_VALUE_SIZE-1] = '\0';
      TLS(Symbol) = S_IDENT;
      return;
    }

    /* Or maybe we just ran out of space */
    if (IsDigit(c)) {
      assert(i >= SAFE_VALUE_SIZE-1);
      TLS(Symbol) = S_PARTIALINT;
      TLS(Value)[SAFE_VALUE_SIZE-1] = '\0';
      return;
    }

    /* Or maybe we saw a . which could indicate one of two things:
       a float literal or .. */
    if (c == '.'){
      /* If the symbol before this integer was S_DOT then 
         we must be in a nested record element expression, so don't 
         look for a float.

      This is a bit fragile  */
      if (TLS(Symbol) == S_DOT || TLS(Symbol) == S_BDOT) {
        TLS(Value)[i]  = '\0';
        TLS(Symbol) = S_INT;
        return;
      }
      
      /* peek ahead to decide which */
      GET_CHAR();
      if (*TLS(In) == '.') {
        /* It was .. */
        UNGET_CHAR(*TLS(In));
        TLS(Symbol) = S_INT;
        TLS(Value)[i] = '\0';
        return;
      }


      /* Not .. Put back the character we peeked at */
      UNGET_CHAR(*TLS(In));
      /* Now the . must be part of our number
         store it and move on */
      TLS(Value)[i++] = c;
      c = GetCleanedChar(&wasEscaped);
    }

    else {
      /* Anything else we see tells us that the token is done */
      TLS(Value)[i]  = '\0';
      TLS(Symbol) = S_INT;
      return;
    }
  }



  /* The only case in which we fall through to here is when
     we have read zero or more digits, followed by . which is not part of a .. token
     or we were called with StartingStatus >= 2 so we read at least that much in
     a previous token */


  if (StartingStatus< 4) {
    /* When we get here we have read (either in this token or a previous S_PARTIALFLOAT*)
       possibly some digits, a . and possibly some more digits, but not an e,E,d,D,q or Q */

    /* read digits */
    for (; !wasEscaped && IsDigit(c) && i < SAFE_VALUE_SIZE-1; i++) {
      TLS(Value)[i] = c;
      seenADigit = 1;
      c = GetCleanedChar(&wasEscaped);
    }
    /* If we found an identifier type character in this context could be an error
      or the start of one of the allowed trailing marker sequences */
    if (wasEscaped || (IsIdent(c)  && c != 'e' && c != 'E' && c != 'D' && c != 'q' &&
                       c != 'd' && c != 'Q')) {

      if (!seenADigit)
        SyntaxError("Badly formed number: need a digit before or after the decimal point");
      /* We allow one letter on the end of the numbers -- could be an i,
       C99 style */
      if (!wasEscaped) {
        if (IsAlpha(c)) {
          TLS(Value)[i++] = c;
          c = GetCleanedChar(&wasEscaped);
        }
        /* independently of that, we allow an _ signalling immediate conversion */
        if (c == '_') {
          TLS(Value)[i++] = c;
          c = GetCleanedChar(&wasEscaped);
          /* After which there may be one character signifying the conversion style */
          if (IsAlpha(c)) {
            TLS(Value)[i++] = c;
            c = GetCleanedChar(&wasEscaped);
          }
        }
        /* Now if the next character is alphanumerical, or an identifier type symbol then we
           really do have an error, otherwise we return a result */
        if (!IsIdent(c) && !IsDigit(c)) {
          TLS(Value)[i] = '\0';
          TLS(Symbol) = S_FLOAT;
          return;
        }
      }
      SyntaxError("Badly formed number");
    }
    /* If the next thing is the start of the exponential notation,
       read it now -- we have left enough space at the end of the buffer even if we
       left the previous loop because of overflow */
    if (IsAlpha(c)) {
        if (!seenADigit)
          SyntaxError("Badly formed number: need a digit before or after the decimal point");
        seenExp = 1;
        TLS(Value)[i++] = c;
        c = GetCleanedChar(&wasEscaped);
        if (!wasEscaped && (c == '+' || c == '-'))
          {
            TLS(Value)[i++] = c;
            c = GetCleanedChar(&wasEscaped);
          }
      }

    /* Now deal with full buffer case */
    if (i >= SAFE_VALUE_SIZE -1) {
      TLS(Symbol) = seenExp ? S_PARTIALFLOAT3 : S_PARTIALFLOAT2;
      TLS(Value)[i] = '\0';
      return;
    }

    /* Either we saw an exponent indicator, or we hit end of token
       deal with the end of token case */
    if (!seenExp) {
      if (!seenADigit)
        SyntaxError("Badly formed number: need a digit before or after the decimal point");
      /* Might be a conversion marker */
      if (!wasEscaped) {
        if (IsAlpha(c) && c != 'e' && c != 'E' && c != 'd' && c != 'D' && c != 'q' && c != 'Q') {
          TLS(Value)[i++] = c;
          c = GetCleanedChar(&wasEscaped);
        }
        /* independently of that, we allow an _ signalling immediate conversion */
        if (c == '_') {
          TLS(Value)[i++] = c;
          c = GetCleanedChar(&wasEscaped);
          /* After which there may be one character signifying the conversion style */
          if (IsAlpha(c))
            TLS(Value)[i++] = c;
          c = GetCleanedChar(&wasEscaped);
        }
        /* Now if the next character is alphanumerical, or an identifier type symbol then we
           really do have an error, otherwise we return a result */
        if (!IsIdent(c) && !IsDigit(c)) {
          TLS(Value)[i] = '\0';
          TLS(Symbol) = S_FLOAT;
          return;
        }
      }
      SyntaxError("Badly Formed Number");
    }

  }

  /* Here we are into the unsigned exponent of a number
     in scientific notation, so we just read digits */
  for (; !wasEscaped && IsDigit(c) && i < SAFE_VALUE_SIZE-1; i++) {
    TLS(Value)[i] = c;
    seenExpDigit = 1;
    c = GetCleanedChar(&wasEscaped);
  }

  /* Look out for a single alphabetic character on the end
     which could be a conversion marker */
  if (seenExpDigit) {
    if (IsAlpha(c)) {
      TLS(Value)[i] = c;
      c = GetCleanedChar(&wasEscaped);
      TLS(Value)[i+1] = '\0';
      TLS(Symbol) = S_FLOAT;
      return;
    }
    if (c == '_') {
      TLS(Value)[i++] = c;
      c = GetCleanedChar(&wasEscaped);
      /* After which there may be one character signifying the conversion style */
      if (IsAlpha(c)) {
        TLS(Value)[i++] = c;
        c = GetCleanedChar(&wasEscaped);
      }
      TLS(Value)[i] = '\0';
      TLS(Symbol) = S_FLOAT;
      return;
    }
  }

  /* If we ran off the end */
  if (i >= SAFE_VALUE_SIZE -1) {
    TLS(Symbol) = seenExpDigit ? S_PARTIALFLOAT4 : S_PARTIALFLOAT3;
    TLS(Value)[i] = '\0';
    return;
  }

  /* Otherwise this is the end of the token */
  if (!seenExpDigit)
    SyntaxError("Badly Formed Number: need at least one digit in the exponent");
  TLS(Symbol) = S_FLOAT;
  TLS(Value)[i] = '\0';
  return;
}


/*******************************************************************************
 **
 *F  GetEscapedChar()   . . . . . . . . . . . . . . . . get an escaped character
 **
 **  'GetEscapedChar' reads an escape sequence from the current input file into
 **  the variable *dst.
 **
 */
static inline Char GetOctalDigits( void )
{
    Char c;

    if ( *TLS(In) < '0' || *TLS(In) > '7' )
        SyntaxError("Expecting octal digit");
    c = 8 * (*TLS(In) - '0');
    GET_CHAR();
    if ( *TLS(In) < '0' || *TLS(In) > '7' )
        SyntaxError("Expecting octal digit");
    c = c + (*TLS(In) - '0');

    return c;
}


/****************************************************************************
 **
 *F  CharHexDigit( <ch> ) . . . . . . . . . turn a single hex digit into Char
 **
 */
static inline Char CharHexDigit( const Char ch ) {
    if (ch >= 'a') {
        return (ch - 'a' + 10);
    } else if (ch >= 'A') {
        return (ch - 'A' + 10);
    } else {
        return (ch - '0');
    }
};

Char GetEscapedChar( void )
{
  Char c;

  c = 0;

  if ( *TLS(In) == 'n'  )       c = '\n';
  else if ( *TLS(In) == 't'  )  c = '\t';
  else if ( *TLS(In) == 'r'  )  c = '\r';
  else if ( *TLS(In) == 'b'  )  c = '\b';
  else if ( *TLS(In) == '>'  )  c = '\01';
  else if ( *TLS(In) == '<'  )  c = '\02';
  else if ( *TLS(In) == 'c'  )  c = '\03';
  else if ( *TLS(In) == '"'  )  c = '"';
  else if ( *TLS(In) == '\\' )  c = '\\';
  else if ( *TLS(In) == '\'' )  c = '\'';
  else if ( *TLS(In) == '0'  ) {
    /* from here we can either read a hex-escape or three digit
       octal numbers */
    GET_CHAR();
    if (*TLS(In) == 'x') {
        GET_CHAR();
        if (!IsHexDigit(*TLS(In))) {
            SyntaxError("Expecting hexadecimal digit");
        }
        c = 16 * CharHexDigit(*TLS(In));
        GET_CHAR();
        if (!IsHexDigit(*TLS(In))) {
            SyntaxError("Expecting hexadecimal digit");
        }
        c += CharHexDigit(*TLS(In));
    } else if (*TLS(In) >= '0' && *TLS(In) <= '7' ) {
        c += GetOctalDigits();
    } else {
        SyntaxError("Expecting hexadecimal escape, or two more octal digits");
    }
  } else if ( *TLS(In) >= '1' && *TLS(In) <= '7' ) {
    /* escaped three digit octal numbers are allowed in input */
    c = 64 * (*TLS(In) - '0');
    GET_CHAR();
    c += GetOctalDigits();
  } else {
      /* Following discussions on pull-request #612, this warning is currently
         disabled for backwards compatibility; some code relies on this behaviour
         and tests break with the warning enabled */
      /*
      if (IsAlpha(*TLS(In)))
          SyntaxWarning("Alphabet letter after \\");
      */
      c = *TLS(In);
  }
  return c;
}

/****************************************************************************
 **
 *F  GetStr()  . . . . . . . . . . . . . . . . . . . . . . get a string, local
 **
 **  'GetStr' reads  a  string from the  current input file into  the variable
 **  'TLS(Value)' and sets 'Symbol'   to  'S_STRING'.  The opening double quote '"'
 **  of the string is the current character pointed to by 'In'.
 **
 **  A string is a sequence of characters delimited  by double quotes '"'.  It
 **  must not include  '"' or <newline>  characters, but the  escape sequences
 **  '\"' or '\n' can  be used instead.  The  escape sequence  '\<newline>' is
 **  ignored, making it possible to split long strings over multiple lines.
 **
 **  An error is raised if the string includes a <newline> character or if the
 **  file ends before the closing '"'.
 **
 **  When TLS(Value) is  completely filled we have to check  if the reading of
 **  the string is  complete or not to decide  between Symbol=S_STRING or
 **  S_PARTIALSTRING.
 */
void GetStr ( void )
{
  Int                 i = 0, fetch;

  /* Avoid substitution of '?' in beginning of GetLine chunks */
  TLS(HELPSubsOn) = 0;

  /* read all characters into 'Value'                                    */
  for ( i = 0; i < SAFE_VALUE_SIZE-1 && *TLS(In) != '"'
           && *TLS(In) != '\n' && *TLS(In) != '\377'; i++ ) {

    fetch = 1;
    /* handle escape sequences                                         */
    if ( *TLS(In) == '\\' ) {
      GET_CHAR();
      /* if next is another '\\' followed by '\n' it must be ignored */
      while ( *TLS(In) == '\\' ) {
        GET_CHAR();
        if ( *TLS(In) == '\n' )
          GET_CHAR();
        else {
          UNGET_CHAR( '\\' );
          break;
        }
      }
      if      ( *TLS(In) == '\n' )  i--;
      else if ( *TLS(In) == '\r' )  {
        GET_CHAR();
        if  ( *TLS(In) == '\n' )  i--;
        else  {TLS(Value)[i] = '\r'; fetch = 0;}
      } else {
          TLS(Value)[i] = GetEscapedChar();
      }
    }

    /* put normal chars into 'Value' but only if there is room         */
    else {
      TLS(Value)[i] = *TLS(In);
    }

    /* read the next character                                         */
    if (fetch) GET_CHAR();

  }

  /* XXX although we have ValueLen we need trailing \000 here,
     in gap.c, function FuncMAKE_INIT this is still used as C-string
     and long integers and strings are not yet supported!    */
  TLS(Value)[i] = '\0';

  /* check for error conditions                                          */
  if ( *TLS(In) == '\n'  )
    SyntaxError("String must not include <newline>");
  if ( *TLS(In) == '\377' )
    SyntaxError("String must end with \" before end of file");

  /* set length of string, set 'Symbol' and skip trailing '"'            */
  TLS(ValueLen) = i;
  if ( i < SAFE_VALUE_SIZE-1 )  {
    TLS(Symbol) = S_STRING;
    if ( *TLS(In) == '"' )  GET_CHAR();
  }
  else
    TLS(Symbol) = S_PARTIALSTRING;

  /* switching on substitution of '?' */
  TLS(HELPSubsOn) = 1;
}

/****************************************************************************
 **
 *F  GetTripStr()  . . . . . . . . . . . . .get a triple quoted string, local
 **
 **  'GetTripStr' reads a triple-quoted string from the  current input file
 **  into  the variable 'Value' and sets 'Symbol'   to  'S_STRING'.
 **  The last member of the opening triple quote '"'
 **  of the string is the current character pointed to by 'In'.
 **
 **  A triple quoted string is any sequence of characters which is terminated
 **  by """. No escaping is performed.
 **
 **  An error is raised if the file ends before the closing """.
 **
 **  When Value is  completely filled we have to check  if the reading of
 **  the string is  complete or not to decide  between Symbol=S_STRING or
 **  S_PARTIALTRIPLESTRING.
 */
void GetTripStr ( void )
{
  Int                 i = 0;

  /* Avoid substitution of '?' in beginning of GetLine chunks */
  TLS(HELPSubsOn) = 0;
  
  /* print only a partial prompt while reading a triple string           */
  if ( !SyQuiet )
    TLS(Prompt) = "> ";
  else
    TLS(Prompt) = "";
  
  /* read all characters into 'Value'                                    */
  for ( i = 0; i < SAFE_VALUE_SIZE-1 && *TLS(In) != '\377'; i++ ) {
    // Only thing to check for is a triple quote.
    
    if ( *TLS(In) == '"') {
        GET_CHAR();
        if (*TLS(In) == '"') {
            GET_CHAR();
            if(*TLS(In) == '"' ) {
                break;
            }
            TLS(Value)[i] = '"';
            i++;
        }
        TLS(Value)[i] = '"';
        i++;
    }
    TLS(Value)[i] = *TLS(In);


    /* read the next character                                         */
    GET_CHAR();
  }

  /* XXX although we have ValueLen we need trailing \000 here,
     in gap.c, function FuncMAKE_INIT this is still used as C-string
     and long integers and strings are not yet supported!    */
  TLS(Value)[i] = '\0';

  /* check for error conditions                                          */
  if ( *TLS(In) == '\377' )
    SyntaxError("String must end with \" before end of file");

  /* set length of string, set 'Symbol' and skip trailing '"'            */
  TLS(ValueLen) = i;
  if ( i < SAFE_VALUE_SIZE-1 )  {
    TLS(Symbol) = S_STRING;
    if ( *TLS(In) == '"' )  GET_CHAR();
  }
  else
    TLS(Symbol) = S_PARTIALTRIPSTRING;

  /* switching on substitution of '?' */
  TLS(HELPSubsOn) = 1;
}

/****************************************************************************
 **
 *F  GetMaybeTripStr()  . . . . . . . . . . . . . . . . . get a string, local
 **
 **  'GetMaybeTripStr' decides if we are reading a single quoted string,
 **  or a triple quoted string.
 */

void GetMaybeTripStr ( void )
{
    /* Avoid substitution of '?' in beginning of GetLine chunks */
    TLS(HELPSubsOn) = 0;
    
    /* This is just a normal string! */
    if ( *TLS(In) != '"' ) {
        GetStr();
        return;
    }
    
    GET_CHAR();
    /* This was just an empty string! */
    if ( *TLS(In) != '"' ) {
        TLS(Value)[0] = '\0';
        TLS(ValueLen) = 0;
        TLS(Symbol) = S_STRING;
        TLS(HELPSubsOn) = 1;
        return;
    }
    
    GET_CHAR();
    /* Now we know we are reading a triple string */
    GetTripStr();
}


/****************************************************************************
 **
 *F  GetChar() . . . . . . . . . . . . . . . . . get a single character, local
 **
 **  'GetChar' reads the next  character from the current input file  into the
 **  variable 'TLS(Value)' and sets 'Symbol' to 'S_CHAR'.  The opening single quote
 **  '\'' of the character is the current character pointed to by 'In'.
 **
 **  A  character is  a  single character delimited by single quotes '\''.  It
 **  must not  be '\'' or <newline>, but  the escape  sequences '\\\'' or '\n'
 **  can be used instead.
 */
void GetChar ( void )
{
  /* skip '\''                                                           */
  GET_CHAR();

  /* handle escape equences                                              */
  if ( *TLS(In) == '\\' ) {
    GET_CHAR();
    TLS(Value)[0] = GetEscapedChar();
  }
  else if ( *TLS(In) == '\n' ) {
    SyntaxError("Newline not allowed in character literal");
  }
  /* put normal chars into 'TLS(Value)'                                  */
  else {
    TLS(Value)[0] = *TLS(In);
  }

  /* read the next character                                             */
  GET_CHAR();

  /* check for terminating single quote                                  */
  if ( *TLS(In) != '\'' )
    SyntaxError("Missing single quote in character constant");

  /* skip the closing quote                                              */
  TLS(Symbol) = S_CHAR;
  if ( *TLS(In) == '\'' )  GET_CHAR();

}


/****************************************************************************
 **
 *F  GetSymbol() . . . . . . . . . . . . . . . . .  get the next symbol, local
 **
 **  'GetSymbol' reads  the  next symbol from   the  input,  storing it in the
 **  variable 'Symbol'.  If 'Symbol' is  'S_IDENT', 'S_INT' or 'S_STRING'  the
 **  value of the symbol is stored in the variable 'TLS(Value)'.  'GetSymbol' first
 **  skips all <space>, <tab> and <newline> characters and comments.
 **
 **  After reading  a  symbol the current  character   is the first  character
 **  beyond that symbol.
 */
void GetSymbol ( void )
{
  /* special case if reading of a long token is not finished */
  if (TLS(Symbol) == S_PARTIALSTRING) {
    GetStr();
    return;
  }
  
  if (TLS(Symbol) == S_PARTIALTRIPSTRING) {
      GetTripStr();
      return;
  }
  
  if (TLS(Symbol) == S_PARTIALINT) {
    if (TLS(Value)[0] == '\0')
      GetNumber(0);
    else
      GetNumber(1);
    return;
  }
  if (TLS(Symbol) == S_PARTIALFLOAT1) {
    GetNumber(2);
    return;
  }

  if (TLS(Symbol) == S_PARTIALFLOAT2) {
    GetNumber(3);
    return;
  }
  if (TLS(Symbol) == S_PARTIALFLOAT3) {
    GetNumber(4);
    return;
  }

  if (TLS(Symbol) == S_PARTIALFLOAT4) {
    GetNumber(5);
    return;
  }


  /* if no character is available then get one                           */
  if ( *TLS(In) == '\0' )
    { TLS(In)--;
      GET_CHAR();
    }

  /* skip over <spaces>, <tabs>, <newlines> and comments                 */
  while (*TLS(In)==' '||*TLS(In)=='\t'||*TLS(In)=='\n'||*TLS(In)=='\r'||*TLS(In)=='\f'||*TLS(In)=='#') {
    if ( *TLS(In) == '#' ) {
      while ( *TLS(In) != '\n' && *TLS(In) != '\r' && *TLS(In) != '\377' )
        GET_CHAR();
    }
    GET_CHAR();
  }

  /* switch according to the character                                   */
  switch ( *TLS(In) ) {

  case '.':   TLS(Symbol) = S_DOT;                         GET_CHAR();
    /*            if ( *TLS(In) == '\\' ) { GET_CHAR();
            if ( *TLS(In) == '\n' ) { GET_CHAR(); } }   */
    if ( *TLS(In) == '.' ) { 
            TLS(Symbol) = S_DOTDOT; GET_CHAR();
            if ( *TLS(In) == '.') {
                    TLS(Symbol) = S_DOTDOTDOT; GET_CHAR();
            }
    }
    break;

  case '!':   TLS(Symbol) = S_ILLEGAL;                     GET_CHAR();
    if ( *TLS(In) == '\\' ) { GET_CHAR();
      if ( *TLS(In) == '\n' ) { GET_CHAR(); } }
    if ( *TLS(In) == '.' ) { TLS(Symbol) = S_BDOT;    GET_CHAR();  break; }
    if ( *TLS(In) == '[' ) { TLS(Symbol) = S_BLBRACK; GET_CHAR();  break; }
    if ( *TLS(In) == '{' ) { TLS(Symbol) = S_BLBRACE; GET_CHAR();  break; }
    break;
  case '[':   TLS(Symbol) = S_LBRACK;                      GET_CHAR();  break;
  case ']':   TLS(Symbol) = S_RBRACK;                      GET_CHAR();  break;
  case '{':   TLS(Symbol) = S_LBRACE;                      GET_CHAR();  break;
  case '}':   TLS(Symbol) = S_RBRACE;                      GET_CHAR();  break;
  case '(':   TLS(Symbol) = S_LPAREN;                      GET_CHAR();  break;
  case ')':   TLS(Symbol) = S_RPAREN;                      GET_CHAR();  break;
  case ',':   TLS(Symbol) = S_COMMA;                       GET_CHAR();  break;

  case ':':   TLS(Symbol) = S_COLON;                       GET_CHAR();
    if ( *TLS(In) == '\\' ) {
      GET_CHAR();
      if ( *TLS(In) == '\n' )
        { GET_CHAR(); }
    }
    if ( *TLS(In) == '=' ) { TLS(Symbol) = S_ASSIGN;  GET_CHAR(); break; }
    if ( TLS(In)[0] == ':' && TLS(In)[1] == '=') {
      TLS(Symbol) = S_INCORPORATE; GET_CHAR(); GET_CHAR(); break;
    }
    break;

  case ';':   TLS(Symbol) = S_SEMICOLON;                   GET_CHAR();  break;

  case '=':   TLS(Symbol) = S_EQ;                          GET_CHAR();  break;
  case '<':   TLS(Symbol) = S_LT;                          GET_CHAR();
    if ( *TLS(In) == '\\' ) { GET_CHAR();
      if ( *TLS(In) == '\n' ) { GET_CHAR(); } }
    if ( *TLS(In) == '=' ) { TLS(Symbol) = S_LE;      GET_CHAR();  break; }
    if ( *TLS(In) == '>' ) { TLS(Symbol) = S_NE;      GET_CHAR();  break; }
    break;
  case '>':   TLS(Symbol) = S_GT;                          GET_CHAR();
    if ( *TLS(In) == '\\' ) { GET_CHAR();
      if ( *TLS(In) == '\n' ) { GET_CHAR(); } }
    if ( *TLS(In) == '=' ) { TLS(Symbol) = S_GE;      GET_CHAR();  break; }
    break;

  case '+':   TLS(Symbol) = S_PLUS;                        GET_CHAR();  break;
  case '-':   TLS(Symbol) = S_MINUS;                       GET_CHAR();
    if ( *TLS(In) == '\\' ) { GET_CHAR();
      if ( *TLS(In) == '\n' ) { GET_CHAR(); } }
    if ( *TLS(In) == '>' ) { TLS(Symbol)=S_MAPTO;     GET_CHAR();  break; }
    break;
  case '*':   TLS(Symbol) = S_MULT;                        GET_CHAR();  break;
  case '/':   TLS(Symbol) = S_DIV;                         GET_CHAR();  break;
  case '^':   TLS(Symbol) = S_POW;                         GET_CHAR();  break;
#ifdef HPCGAP
  case '`':   TLS(Symbol) = S_BACKQUOTE;                   GET_CHAR();  break;
#endif

  case '"':                        GET_CHAR(); GetMaybeTripStr();  break;
  case '\'':                                          GetChar();   break;
  case '\\':                                          GetIdent();  break;
  case '_':                                           GetIdent();  break;
  case '@':                                           GetIdent();  break;
  case '~':   TLS(Value)[0] = '~';  TLS(Value)[1] = '\0';
    TLS(Symbol) = S_IDENT;                       GET_CHAR();  break;

  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    GetNumber(0);    break;

  case '\377': TLS(Symbol) = S_EOF;                        *TLS(In) = '\0';  break;

  default :   if ( IsAlpha(*TLS(In)) )                   { GetIdent();  break; }
    TLS(Symbol) = S_ILLEGAL;                     GET_CHAR();  break;
  }
}


/****************************************************************************
 **

 *F * * * * * * * * * * * * *  output functions  * * * * * * * * * * * * * * *
 */


/****************************************************************************
 **

 *V  WriteAllFunc  . . . . . . . . . . . . . . . . . . . . . . . .  'WriteAll'
 */
Obj WriteAllFunc;


/****************************************************************************
 **
 *F  PutLine2( <output>, <line>, <len> )  . . . . . . . . . print a line, local
 **
 **  Introduced  <len> argument. Actually in all cases where this is called one
 **  knows the length of <line>, so it is not necessary to compute it again
 **  with the inefficient C- strlen.  (FL)
 */


void PutLine2(
        TypOutputFile *         output,
        const Char *            line,
        UInt                    len )
{
  Obj                     str;
  UInt                    lstr;
  if ( output->isstream ) {
    /* special handling of string streams, where we can copy directly */
    if (output->isstringstream) {
      str = ADDR_OBJ(output->stream)[1];
      lstr = GET_LEN_STRING(str);
      GROW_STRING(str, lstr+len);
      memcpy((void *) (CHARS_STRING(str) + lstr), line, len);
      SET_LEN_STRING(str, lstr + len);
      *(CHARS_STRING(str) + lstr + len) = '\0';
      CHANGED_BAG(str);
      return;
    }

    /* Space for the null is allowed for in GAP strings */
    C_NEW_STRING( str, len, line );

    /* now delegate to library level */
    CALL_2ARGS( WriteAllFunc, output->stream, str );
  }
  else {
    SyFputs( line, output->file );
  }
}


/****************************************************************************
 **
 *F  PutLineTo ( stream, len ) . . . . . . . . . . . . . . print a line, local
 **
 **  'PutLineTo'  prints the first len characters of the current output
 **  line   'stream->line' to <stream>
 **  It  is  called from 'PutChrTo'.
 **
 **  'PutLineTo' also compares the output line with the  next line from the test
 **  input file 'TestInput' if 'TestInput' is not 0.  If  this input line does
 **  not starts with 'gap>' and the rest  of the line  matches the output line
 **  then the output line is not printed and the input line is discarded.
 **
 **  'PutLineTo'  also echoes the  output  line  to the  logfile 'OutputLog' if
 **  'OutputLog' is not 0 and the output file is '*stdout*' or '*errout*'.
 **
 **  Finally 'PutLineTo' checks whether the user has hit '<ctr>-C' to  interrupt
 **  the printing.
 */
void PutLineTo ( KOutputStream stream, UInt len )
{
  Char *          p;
  UInt lt,ls;     /* These are supposed to hold string lengths */

  /* if in test mode and the next input line matches print nothing       */
  if ( TLS(TestInput) != 0 && TLS(TestOutput) == stream ) {
    if ( TLS(TestLine)[0] == '\0' ) {
      if ( ! GetLine2( TLS(TestInput), TLS(TestLine), sizeof(TLS(TestLine)) ) ) {
        TLS(TestLine)[0] = '\0';
      }
      TLS(TestInput)->number++;
    }

    /* Note that TLS(TestLine) is ended by a \n, but stream->line need not! */

    lt = strlen(TLS(TestLine));   /* this counts including the newline! */
    p = TLS(TestLine) + (lt-2);
    /* this now points to the last char before \n in the line! */
    while ( TLS(TestLine) <= p && ( *p == ' ' || *p == '\t' ) ) {
      p[1] = '\0';  p[0] = '\n';  p--; lt--;
    }
    /* lt is still the correct string length including \n */
    ls = strlen(stream->line);
    p = stream->line + (ls-1);
    /* this now points to the last char of the string, could be a \n */
    if (*p == '\n') {
      p--;   /* now we point before that newline character */
      while ( stream->line <= p && ( *p == ' ' || *p == '\t' ) ) {
        p[1] = '\0';  p[0] = '\n';  p--; ls--;
      }
    }
    /* ls is still the correct string length including a possible \n */
    if ( ! strncmp( TLS(TestLine), stream->line, ls ) ) {
      if (ls < lt)
        memmove(TLS(TestLine),TLS(TestLine) + ls,lt-ls+1);
      else
        TLS(TestLine)[0] = '\0';
    }
    else {
      char obuf[80];
      /* snprintf(obuf, sizeof(obuf), "+ 5%i bad example:\n+ ", (int)TLS(TestInput)->number); */
      snprintf(obuf, sizeof(obuf), "Line %i : \n+ ", (int)TLS(TestInput)->number);
      PutLine2( stream, obuf, strlen(obuf) );
      PutLine2( stream, TLS(Output)->line, strlen(TLS(Output)->line) );
    }
  }

  /* otherwise output this line                                          */
  else {
    PutLine2( stream, stream->line, len );
  }

  /* if neccessary echo it to the logfile                                */
  if ( TLS(OutputLog) != 0 && ! stream->isstream ) {
    if ( stream->file == 1 || stream->file == 3 ) {
      PutLine2( TLS(OutputLog), stream->line, len );
    }
  }
}


/****************************************************************************
 **
 *F  PutChrTo( <stream>, <ch> )  . . . . . . . . . print character <ch>, local
 **
 **  'PutChrTo' prints the single character <ch> to the stream <stream>
 **
 **  'PutChrTo' buffers the  output characters until  either <ch> is  <newline>,
 **  <ch> is '\03' (<flush>) or the buffer fills up.
 **
 **  In the later case 'PutChrTo' has to decide where to  split the output line.
 **  It takes the point at which $linelength - pos + 8 * indent$ is minimal.
 */
/* TL: Int NoSplitLine = 0; */

/* helper function to add a hint about a possible line break;
   a triple (pos, value, indent), such that the minimal (value-pos) wins */
void addLineBreakHint( KOutputStream stream, Int pos, Int val, Int indentdiff )
{
  Int nr, i;
  /* find next free slot */
  for (nr = 0; nr < MAXHINTS && stream->hints[3*nr] != -1; nr++);
  if (nr == MAXHINTS) {
    /* forget the first stored hint */
    for (i = 0; i < 3*MAXHINTS - 3; i++)
       stream->hints[i] =  stream->hints[i+3];
    nr--;
  }
  /* if pos is same as before only relevant if new entry has higher
     priority */
  if ( nr > 0 && stream->hints[3*(nr-1)] == pos )
    nr--;

  if ( stream->indent < pos &&
       (stream->hints[3*nr] == -1 || val < stream->hints[3*(nr)+1]) ) {
    stream->hints[3*nr] = pos;
    stream->hints[3*nr+1] = val;
    stream->hints[3*nr+2] = stream->indent;
    stream->hints[3*nr+3] = -1;
  }
  stream->indent += indentdiff;
}
/* helper function to find line break position,
   returns position nr in stream[hints] or -1 if none found */
Int nrLineBreak( KOutputStream stream )
{
  Int nr=-1, min, i;
  for (i = 0, min = INT_MAX; stream->hints[3*i] != -1; i++)
  {
    if (stream->hints[3*i] > 0 &&
        stream->hints[3*i+1] - stream->hints[3*i] <= min)
    {
      nr = i;
      min = stream->hints[3*i+1] - stream->hints[3*i];
    }
  }
  if (min < INT_MAX)
    return nr;
  else
    return -1;
}



void PutChrTo (
         KOutputStream stream,
         Char                ch )
{
  Int                 i, hint, spos;
  Char                str [MAXLENOUTPUTLINE];


  /* '\01', increment indentation level                                  */
  if ( ch == '\01' ) {

    if (!stream->format)
      return;

    /* add hint to break line  */
    addLineBreakHint(stream, stream->pos, 16*stream->indent, 1);
  }

  /* '\02', decrement indentation level                                  */
  else if ( ch == '\02' ) {

    if (!stream -> format)
      return;

    /* if this is a better place to split the line remember it         */
    addLineBreakHint(stream, stream->pos, 16*stream->indent, -1);
  }

  /* '\03', print line                                                   */
  else if ( ch == '\03' ) {

    /* print the line                                                  */
    if (stream->pos != 0)
      {
        stream->line[ stream->pos ] = '\0';
        PutLineTo(stream, stream->pos );

        /* start the next line                                         */
        stream->pos      = 0;
      }
    /* reset line break hints                                          */
    stream->hints[0] = -1;

  }

  /* <newline> or <return>, print line, indent next                      */
  else if ( ch == '\n' || ch == '\r' ) {

    /* put the character on the line and terminate it                  */
    stream->line[ stream->pos++ ] = ch;
    stream->line[ stream->pos   ] = '\0';

    /* print the line                                                  */
    PutLineTo( stream, stream->pos );

    /* and dump it from the buffer */
    stream->pos = 0;
    if (stream -> format)
      {
        /* indent for next line                                         */
        for ( i = 0;  i < stream->indent; i++ )
          stream->line[ stream->pos++ ] = ' ';
      }
    /* reset line break hints                                       */
    stream->hints[0] = -1;

  }

  /* normal character, room on the current line                          */
  else if ( stream->pos < SyNrCols-2-TLS(NoSplitLine) ) {

    /* put the character on this line                                  */
    stream->line[ stream->pos++ ] = ch;

  }

  else
    {
      /* position to split                                              */
      if ( (hint = nrLineBreak(stream)) != -1 )
        spos = stream->hints[3*hint];
      else
        spos = 0;

      /* if we are going to split at the end of the line, and we are
         formatting discard blanks */
      if ( stream->format && spos == stream->pos && ch == ' ' ) {
        ;
      }

      /* full line, acceptable split position                              */
      else if ( stream->format && spos != 0 ) {

        /* add character to the line, terminate it                         */
        stream->line[ stream->pos++ ] = ch;
        stream->line[ stream->pos++ ] = '\0';

        /* copy the rest after the best split position to a safe place     */
        for ( i = spos; i < stream->pos; i++ )
          str[ i-spos ] = stream->line[ i ];
        str[ i-spos] = '\0';

        /* print line up to the best split position                        */
        stream->line[ spos++ ] = '\n';
        stream->line[ spos   ] = '\0';
        PutLineTo( stream, spos );
        spos--;

        /* indent for the rest                                             */
        stream->pos = 0;
        for ( i = 0; i < stream->hints[3*hint+2]; i++ )
          stream->line[ stream->pos++ ] = ' ';
        spos -= stream->hints[3*hint+2];

        /* copy the rest onto the next line                                */
        for ( i = 0; str[ i ] != '\0'; i++ )
          stream->line[ stream->pos++ ] = str[ i ];
        /* recover line break hints for copied rest                      */
        for ( i = hint+1; stream->hints[3*i] != -1; i++ )
        {
          stream->hints[3*(i-hint-1)] = stream->hints[3*i]-spos;
          stream->hints[3*(i-hint-1)+1] = stream->hints[3*i+1];
          stream->hints[3*(i-hint-1)+2] = stream->hints[3*i+2];
        }
        stream->hints[3*(i-hint-1)] = -1;
      }

      /* full line, no split position                                       */
      else {

        if (stream->format)
          {
            /* append a '\',*/
            stream->line[ stream->pos++ ] = '\\';
            stream->line[ stream->pos++ ] = '\n';
          }
        /* and print the line                                */
        stream->line[ stream->pos   ] = '\0';
        PutLineTo( stream, stream->pos );

        /* add the character to the next line                              */
        stream->pos = 0;
        stream->line[ stream->pos++ ] = ch;

        if (stream->format)
          stream->hints[0] = -1;
      }

    }
}

/****************************************************************************
 **
 *F  FuncToggleEcho( )
 **
*/

Obj FuncToggleEcho( Obj self)
{
  TLS(Input)->echo = 1 - TLS(Input)->echo;
  return (Obj)0;
}

/****************************************************************************
 **
 *F  FuncCPROMPT( )
 **
 **  returns the current `Prompt' as GAP string.
 */
Obj FuncCPROMPT( Obj self)
{
  Obj p;
  C_NEW_STRING_DYN( p, TLS(Prompt) );
  return p;
}

/****************************************************************************
 **
 *F  FuncPRINT_CPROMPT( <prompt> )
 **
 **  prints current `Prompt' if argument <prompt> is not in StringRep, otherwise
 **  uses the content of <prompt> as `Prompt' (at most 80 characters).
 **  (important is the flush character without resetting the cursor column)
 */
Char promptBuf[81];

Obj FuncPRINT_CPROMPT( Obj self, Obj prompt )
{
  if (IS_STRING_REP(prompt)) {
    /* by assigning to Prompt we also tell readline (if used) what the
       current prompt is  */
    strlcpy(promptBuf, CSTR_STRING(prompt), sizeof(promptBuf));
    TLS(Prompt) = promptBuf;
  }
  Pr("%s%c", (Int)TLS(Prompt), (Int)'\03' );
  return (Obj) 0;
}

/****************************************************************************
 **
 *F  Pr( <format>, <arg1>, <arg2> )  . . . . . . . . .  print formatted output
 *F  PrTo( <stream>, <format>, <arg1>, <arg2> )  . . .  print formatted output
 **
 **  'Pr' is the output function. The first argument is a 'printf' like format
 **  string containing   up   to 2  '%'  format   fields,   specifing  how the
 **  corresponding arguments are to be  printed.  The two arguments are passed
 **  as  'Int'   integers.   This  is possible  since every  C object  ('int',
 **  'char', pointers) except 'float' or 'double', which are not used  in GAP,
 **  can be converted to a 'Int' without loss of information.
 **
 **  The function 'Pr' currently support the following '%' format  fields:
 **  '%c'    the corresponding argument represents a character,  usually it is
 **          its ASCII or EBCDIC code, and this character is printed.
 **  '%s'    the corresponding argument is the address of  a  null  terminated
 **          character string which is printed.
 **  '%S'    the corresponding argument is the address of  a  null  terminated
 **          character string which is printed with escapes.
 **  '%C'    the corresponding argument is the address of  a  null  terminated
 **          character string which is printed with C escapes.
 **  '%d'    the corresponding argument is a signed integer, which is printed.
 **          Between the '%' and the 'd' an integer might be used  to  specify
 **          the width of a field in which the integer is right justified.  If
 **          the first character is '0' 'Pr' pads with '0' instead of <space>.
 **  '%i'    is a synonym of %d, in line with recent C library developements
 **  '%I'    print an identifier
 **  '%>'    increment the indentation level.
 **  '%<'    decrement the indentation level.
 **  '%%'    can be used to print a single '%' character. No argument is used.
 **
 **  You must always  cast the arguments to  '(Int)'  to avoid  problems  with
 **  those compilers with a default integer size of 16 instead of 32 bit.  You
 **  must pass 0L if you don't make use of an argument to please lint.
 */

void FormatOutput(void (*put_a_char)(Char c), const Char *format, Int arg1, Int arg2 ) {
  const Char *        p;
  Char *              q;
  Int                 prec,  n;
  Char                fill;

  /* loop over the characters of the <format> string                     */
  for ( p = format; *p != '\0'; p++ ) {

    /* not a '%' character, simply print it                            */
    if ( *p != '%' ) {
      put_a_char( *p );
      continue;
    }

    /* if the character is '%' do something special                    */

    /* first look for a precision field                            */
    p++;
    prec = 0;
    fill = (*p == '0' ? '0' : ' ');
    while ( IsDigit(*p) ) {
      prec = 10 * prec + *p - '0';
      p++;
    }

    /* handle the case of a missing argument                     */
    if (arg1 == 0 && (*p == 's' || *p == 'S' || *p == 'C' || *p == 'I')) {
      put_a_char('<');
      put_a_char('n');
      put_a_char('u');
      put_a_char('l');
      put_a_char('l');
      put_a_char('>');

      /* on to the next argument                                 */
      arg1 = arg2;
    }

    /* '%d' print an integer                                       */
    else if ( *p == 'd'|| *p == 'i' ) {
      int is_neg = (arg1 < 0);
      if ( is_neg ) {
        arg1 = -arg1;
        prec--; /* we loose one digit of output precision for the minus sign */
      }

      /* compute how many characters this number requires    */
      for ( n = 1; n <= arg1/10; n*=10 ) {
        prec--;
      }
      while ( --prec > 0 )  put_a_char(fill);

      if ( is_neg ) {
        put_a_char('-');
      }

      for ( ; n > 0; n /= 10 )
        put_a_char( (Char)(((arg1/n)%10) + '0') );

      /* on to the next argument                                 */
      arg1 = arg2;
    }

    /* '%s' print a string                                         */
    else if ( *p == 's' ) {

      /* compute how many characters this identifier requires    */
      for ( q = (Char*)arg1; *q != '\0' && prec > 0; q++ ) {
        prec--;
      }

      /* if wanted push an appropriate number of <space>-s       */
      while ( prec-- > 0 )  put_a_char(' ');

      /* print the string                                        */
      /* must be careful that line breaks don't go inside
         escaped sequences \n or \123 or similar */
      for ( q = (Char*)arg1; *q != '\0'; q++ ) {
        if (*q == '\\' && TLS(NoSplitLine) == 0) {
          if (*(q+1) < '8' && *(q+1) >= '0')
            TLS(NoSplitLine) = 3;
          else
            TLS(NoSplitLine) = 1;
        }
        else if (TLS(NoSplitLine) > 0)
          TLS(NoSplitLine)--;
        put_a_char( *q );
      }

      /* on to the next argument                                 */
      arg1 = arg2;
    }

    /* '%S' print a string with the necessary escapes              */
    else if ( *p == 'S' ) {

      /* compute how many characters this identifier requires    */
      for ( q = (Char*)arg1; *q != '\0' && prec > 0; q++ ) {
        if      ( *q == '\n'  ) { prec -= 2; }
        else if ( *q == '\t'  ) { prec -= 2; }
        else if ( *q == '\r'  ) { prec -= 2; }
        else if ( *q == '\b'  ) { prec -= 2; }
        else if ( *q == '\01' ) { prec -= 2; }
        else if ( *q == '\02' ) { prec -= 2; }
        else if ( *q == '\03' ) { prec -= 2; }
        else if ( *q == '"'   ) { prec -= 2; }
        else if ( *q == '\\'  ) { prec -= 2; }
        else                    { prec -= 1; }
      }

      /* if wanted push an appropriate number of <space>-s       */
      while ( prec-- > 0 )  put_a_char(' ');

      /* print the string                                        */
      for ( q = (Char*)arg1; *q != '\0'; q++ ) {
        if      ( *q == '\n'  ) { put_a_char('\\'); put_a_char('n');  }
        else if ( *q == '\t'  ) { put_a_char('\\'); put_a_char('t');  }
        else if ( *q == '\r'  ) { put_a_char('\\'); put_a_char('r');  }
        else if ( *q == '\b'  ) { put_a_char('\\'); put_a_char('b');  }
        else if ( *q == '\01' ) { put_a_char('\\'); put_a_char('>');  }
        else if ( *q == '\02' ) { put_a_char('\\'); put_a_char('<');  }
        else if ( *q == '\03' ) { put_a_char('\\'); put_a_char('c');  }
        else if ( *q == '"'   ) { put_a_char('\\'); put_a_char('"');  }
        else if ( *q == '\\'  ) { put_a_char('\\'); put_a_char('\\'); }
        else                    { put_a_char( *q );               }
      }

      /* on to the next argument                                 */
      arg1 = arg2;
    }

    /* '%C' print a string with the necessary C escapes            */
    else if ( *p == 'C' ) {

      /* compute how many characters this identifier requires    */
      for ( q = (Char*)arg1; *q != '\0' && prec > 0; q++ ) {
        if      ( *q == '\n'  ) { prec -= 2; }
        else if ( *q == '\t'  ) { prec -= 2; }
        else if ( *q == '\r'  ) { prec -= 2; }
        else if ( *q == '\b'  ) { prec -= 2; }
        else if ( *q == '\01' ) { prec -= 3; }
        else if ( *q == '\02' ) { prec -= 3; }
        else if ( *q == '\03' ) { prec -= 3; }
        else if ( *q == '"'   ) { prec -= 2; }
        else if ( *q == '\\'  ) { prec -= 2; }
        else                    { prec -= 1; }
      }

      /* if wanted push an appropriate number of <space>-s       */
      while ( prec-- > 0 )  put_a_char(' ');

      /* print the string                                        */
      for ( q = (Char*)arg1; *q != '\0'; q++ ) {
        if      ( *q == '\n'  ) { put_a_char('\\'); put_a_char('n');  }
        else if ( *q == '\t'  ) { put_a_char('\\'); put_a_char('t');  }
        else if ( *q == '\r'  ) { put_a_char('\\'); put_a_char('r');  }
        else if ( *q == '\b'  ) { put_a_char('\\'); put_a_char('b');  }
        else if ( *q == '\01' ) { put_a_char('\\'); put_a_char('0');
          put_a_char('1');                }
        else if ( *q == '\02' ) { put_a_char('\\'); put_a_char('0');
          put_a_char('2');                }
        else if ( *q == '\03' ) { put_a_char('\\'); put_a_char('0');
          put_a_char('3');                }
        else if ( *q == '"'   ) { put_a_char('\\'); put_a_char('"');  }
        else if ( *q == '\\'  ) { put_a_char('\\'); put_a_char('\\'); }
        else                    { put_a_char( *q );               }
      }

      /* on to the next argument                                 */
      arg1 = arg2;
    }

    /* '%I' print an identifier                                    */
    else if ( *p == 'I' ) {
      int found_keyword = 0;
      int i;

      /* check if q matches a keyword    */
      q = (Char*)arg1;
      for ( i = 0; i < sizeof(AllKeywords)/sizeof(AllKeywords[0]); i++ ) {
        if ( strcmp(q, AllKeywords[i].name) == 0 ) {
          found_keyword = 1;
          break;
        }
      }

      /* compute how many characters this identifier requires    */
      if (found_keyword) {
        prec--;
      }
      for ( q = (Char*)arg1; *q != '\0'; q++ ) {
        if ( !IsIdent(*q) && !IsDigit(*q) ) {
          prec--;
        }
        prec--;
      }

      /* if wanted push an appropriate number of <space>-s       */
      while ( prec-- > 0 ) { put_a_char(' '); }

      /* print the identifier                                    */
      if ( found_keyword ) {
        put_a_char( '\\' );
      }
      for ( q = (Char*)arg1; *q != '\0'; q++ ) {
        if ( !IsIdent(*q) && !IsDigit(*q) ) {
          put_a_char( '\\' );
        }
        put_a_char( *q );
      }

      /* on to the next argument                                 */
      arg1 = arg2;
    }

    /* '%c' print a character                                      */
    else if ( *p == 'c' ) {
      put_a_char( (Char)arg1 );
      arg1 = arg2;
    }

    /* '%%' print a '%' character                                  */
    else if ( *p == '%' ) {
      put_a_char( '%' );
    }

    /* '%>' increment the indentation level                        */
    else if ( *p == '>' ) {
      put_a_char( '\01' );
      while ( --prec > 0 )
        put_a_char( '\01' );
    }

    /* '%<' decrement the indentation level                        */
    else if ( *p == '<' ) {
      put_a_char( '\02' );
      while ( --prec > 0 )
        put_a_char( '\02' );
    }

    /* else raise an error                                         */
    else {
      for ( p = "%format error"; *p != '\0'; p++ )
        put_a_char( *p );
    }

  }

}


/* TL: static KOutputStream TheStream; */

static void putToTheStream( Char c) {
  PutChrTo(TLS(TheStream), c);
}

void PrTo (
           KOutputStream     stream,
           const Char *      format,
           Int                 arg1,
           Int                 arg2 )
{
  KOutputStream savedStream = TLS(TheStream);
  TLS(TheStream) = stream;
  FormatOutput( putToTheStream, format, arg1, arg2);
  TLS(TheStream) = savedStream;
}

void Pr (
         const Char *      format,
         Int                 arg1,
         Int                 arg2 )
{
  PrTo(TLS(Output), format, arg1, arg2);
}

/* TL: static Char *theBuffer; */
/* TL: static UInt theCount; */
/* TL: static UInt theLimit; */

static void putToTheBuffer( Char c)
{
  if (TLS(TheCount) < TLS(TheLimit))
    TLS(TheBuffer)[TLS(TheCount)++] = c;
}

void SPrTo(Char *buffer, UInt maxlen, const Char *format, Int arg1, Int arg2)
{
  Char *savedBuffer = TLS(TheBuffer);
  UInt savedCount = TLS(TheCount);
  UInt savedLimit = TLS(TheLimit);
  TLS(TheBuffer) = buffer;
  TLS(TheCount) = 0;
  TLS(TheLimit) = maxlen;
  FormatOutput(putToTheBuffer, format, arg1, arg2);
  putToTheBuffer('\0');
  TLS(TheBuffer) = savedBuffer;
  TLS(TheCount) = savedCount;
  TLS(TheLimit) = savedLimit;
}


Obj FuncINPUT_FILENAME( Obj self) {
  Obj s;
  if (TLS(Input)) {
    C_NEW_STRING_DYN( s, TLS(Input)->name );
  } else {
    C_NEW_STRING_CONST( s, "*defin*" );
  }
  return s;
}

Obj FuncINPUT_LINENUMBER( Obj self) {
  return INTOBJ_INT(TLS(Input) ? TLS(Input)->number : 0);
}

Obj FuncALL_KEYWORDS(Obj self) {
  Obj l;

  Obj s;
  UInt i;
  l = NEW_PLIST(T_PLIST_EMPTY, 0);
  SET_LEN_PLIST(l,0);
  for (i = 0; i < sizeof(AllKeywords)/sizeof(AllKeywords[0]); i++) {
    C_NEW_STRING_DYN(s,AllKeywords[i].name);
    ASS_LIST(l, i+1, s);
  }
  MakeImmutable(l);
  return l;
}

Obj FuncSET_PRINT_FORMATTING_STDOUT(Obj self, Obj val) {
  if (val == False)
      ((TypOutputFile *)(TLS(OutputFiles)+1))->format = 0;
  else
      ((TypOutputFile *)(TLS(OutputFiles)+1))->format = 1;
  return val;
}



/****************************************************************************
 **
 *F * * * * * * * * * * * * * initialize package * * * * * * * * * * * * * * *
 */

/****************************************************************************
 **
 *V  GVarFuncs . . . . . . . . . . . . . . . . . . list of functions to export
 */
static StructGVarFunc GVarFuncs [] = {

  { "ToggleEcho", 0, "",
    FuncToggleEcho, "src/scanner.c:ToggleEcho" },

  { "CPROMPT", 0, "",
    FuncCPROMPT, "src/scanner.c:CPROMPT" },

  { "PRINT_CPROMPT", 1, "prompt",
    FuncPRINT_CPROMPT, "src/scanner.c:PRINT_CPROMPT" },

  { "INPUT_FILENAME", 0 , "",
    FuncINPUT_FILENAME, "src/scanner.c:INPUT_FILENAME" },

  { "INPUT_LINENUMBER", 0 , "",
    FuncINPUT_LINENUMBER, "src/scanner.c:INPUT_LINENUMBER" },

  { "ALL_KEYWORDS", 0 , "",
    FuncALL_KEYWORDS, "src/scanner.c:ALL_KEYWORDS"},

  { "SET_PRINT_FORMATTING_STDOUT", 1 , "format",
    FuncSET_PRINT_FORMATTING_STDOUT,
    "src/scanner.c:SET_PRINT_FORMATTING_STDOUT"},

  { 0 }

};

/****************************************************************************
 **
 *F  InitLibrary( <module> ) . . . . . . .  initialise library data structures
 */
static Int InitLibrary (
                        StructInitInfo *    module )
{
  /* init filters and functions                                          */
  InitGVarFuncsFromTable( GVarFuncs );

  /* return success                                                      */
  return 0;
}

/****************************************************************************
 **
 *F  InitKernel( <module> )  . . . . . . . . initialise kernel data structures
 */
static Char Cookie[sizeof(TLS(InputFiles))/sizeof(TLS(InputFiles)[0])][9];
static Char MoreCookie[sizeof(TLS(InputFiles))/sizeof(TLS(InputFiles)[0])][9];
static Char StillMoreCookie[sizeof(TLS(InputFiles))/sizeof(TLS(InputFiles)[0])][9];

static Int InitKernel (
    StructInitInfo *    module )
{
    Int                 i;

    TLS(Input) = TLS(InputFiles);
    TLS(Input)--;
    (void)OpenInput(  "*stdin*"  );
    TLS(Input)->echo = 1; /* echo stdin */
    TLS(Output) = 0L;
    (void)OpenOutput( "*stdout*" );

    TLS(InputLog)  = 0;  TLS(OutputLog)  = 0;
    TLS(TestInput) = 0;  TLS(TestOutput) = 0;

    /* initialize cookies for streams                                      */
    /* also initialize the cookies for the GAP strings which hold the
       latest lines read from the streams  and the name of the current input file*/
    for ( i = 0;  i < sizeof(TLS(InputFiles))/sizeof(TLS(InputFiles)[0]);  i++ ) {
      Cookie[i][0] = 's';  Cookie[i][1] = 't';  Cookie[i][2] = 'r';
      Cookie[i][3] = 'e';  Cookie[i][4] = 'a';  Cookie[i][5] = 'm';
      Cookie[i][6] = ' ';  Cookie[i][7] = '0'+i;
      Cookie[i][8] = '\0';
      InitGlobalBag(&(TLS(InputFiles)[i].stream), &(Cookie[i][0]));

      MoreCookie[i][0] = 's';  MoreCookie[i][1] = 'l';  MoreCookie[i][2] = 'i';
      MoreCookie[i][3] = 'n';  MoreCookie[i][4] = 'e';  MoreCookie[i][5] = ' ';
      MoreCookie[i][6] = ' ';  MoreCookie[i][7] = '0'+i;
      MoreCookie[i][8] = '\0';
      InitGlobalBag(&(TLS(InputFiles)[i].sline), &(MoreCookie[i][0]));

      StillMoreCookie[i][0] = 'g';  StillMoreCookie[i][1] = 'a';  StillMoreCookie[i][2] = 'p';
      StillMoreCookie[i][3] = 'n';  StillMoreCookie[i][4] = 'a';  StillMoreCookie[i][5] = 'm';
      StillMoreCookie[i][6] = 'e';  StillMoreCookie[i][7] = '0'+i;
      StillMoreCookie[i][8] = '\0';
      InitGlobalBag(&(TLS(InputFiles)[i].gapname), &(StillMoreCookie[i][0]));
    }

    /* tell GASMAN about the global bags                                   */
    InitGlobalBag(&(TLS(LogFile).stream),        "src/scanner.c:LogFile"        );
    InitGlobalBag(&(TLS(LogStream).stream),      "src/scanner.c:LogStream"      );
    InitGlobalBag(&(TLS(InputLogStream).stream), "src/scanner.c:InputLogStream" );
    InitGlobalBag(&(TLS(OutputLogStream).stream),"src/scanner.c:OutputLogStream");


    /* import functions from the library                                   */
    ImportFuncFromLibrary( "ReadLine", &ReadLineFunc );
    ImportFuncFromLibrary( "WriteAll", &WriteAllFunc );
    ImportFuncFromLibrary( "IsInputTextStringRep", &IsStringStream );
    InitCopyGVar( "PrintPromptHook", &PrintPromptHook );
    InitCopyGVar( "EndLineHook", &EndLineHook );
    InitFopyGVar( "PrintFormattingStatus", &PrintFormattingStatus);

    InitHdlrFuncsFromTable( GVarFuncs );
    /* return success                                                      */
    return 0;
}


/****************************************************************************
 **
 *F  InitInfoScanner() . . . . . . . . . . . . . . . . table of init functions
 */
static StructInitInfo module = {
  MODULE_BUILTIN,                     /* type                           */
  "scanner",                          /* name                           */
  0,                                  /* revision entry of c file       */
  0,                                  /* revision entry of h file       */
  0,                                  /* version                        */
  0,                                  /* crc                            */
  InitKernel,                         /* initKernel                     */
  InitLibrary,                        /* initLibrary                    */
  0,                                  /* checkInit                      */
  0,                                  /* preSave                        */
  0,                                  /* postSave                       */
  0                                   /* postRestore                    */
};

StructInitInfo * InitInfoScanner ( void )
{
  return &module;
}

/****************************************************************************
 **
 *F  InitScannerTLS() . . . . . . . . . . . . . . . . . . . . . initialize TLS
 *F  DestroyScannerTLS()  . . . . . . . . . . . . . . . . . . . .  destroy TLS
 */

void InitScannerState(GlobalState *state)
{
  state->HELPSubsOn = 1;
}

void DestroyScannerState(GlobalState *state)
{
}


/****************************************************************************
 **
 *E  scanner.c . . . . . . . . . . . . . . . . . . . . . . . . . . . ends here
 */
