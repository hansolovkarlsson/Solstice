program TestProctypeReturnMethod;

// A class METHOD (not just a plain function) can also return a named
// procedural type - its own return-value assignment goes through the
// same shared statement() code path as an ordinary function, for free.
// Calling the method's returned value needs a fallback inside
// parse_proc_value() (a class instance expression like 'f.MakeHandler()'
// isn't a bare proc name, so the specialized parser hands off to a
// general expression() and lets the ordinary assignment type check
// validate the result - see parser.c). Triple(4) = 12.

type
  TProc = function(x: integer): integer;

  TFactory = class
    function MakeHandler: TProc;
  end;

var
  f: TFactory;
  h: TProc;

function Triple(x: integer): integer;
begin
  Triple := x * 3;
end;

function TFactory.MakeHandler;
begin
  MakeHandler := Triple;
end;

begin
  new(f);
  h := f.MakeHandler();
  writeln(h(4));
  dispose(f);
end.
