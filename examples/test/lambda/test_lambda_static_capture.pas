program TestLambdaStaticCapture;

{ A lambda reading/writing an enclosing procedure's 'static' local -
  still allowed (a static local is already a hidden global under the
  hood, see docs/LANGUAGE.md and test_nested_static.pas), NOT rejected
  by the capture check. callCount goes 1, 2, 3. }

type
    TTick = procedure;

procedure Outer;
    var
        static callCount: integer;
        tick: TTick;
begin
    tick := procedure begin
        callCount := callCount + 1;
        writeln('callCount=', callCount);
    end;
    tick;
    tick;
    tick;
end;

begin
    Outer;
end.
