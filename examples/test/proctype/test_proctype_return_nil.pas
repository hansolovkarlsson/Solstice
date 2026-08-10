program TestProctypeReturnNil;

// A function can return 'nil' as its procedural value, exactly like an
// ordinary procedural-type assignment target can - unaffected by the
// call-vs-reference disambiguation, since 'nil' is a separate branch
// entirely. Prints 'nil'.

type
  TProc = function(x: integer): integer;

var h: TProc;

function GetNothing: TProc;
begin
  GetNothing := nil;
end;

begin
  h := GetNothing();
  if h = nil then
    writeln('nil')
  else
    writeln('not nil');
end.
