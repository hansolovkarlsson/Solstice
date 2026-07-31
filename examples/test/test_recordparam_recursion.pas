program TestRecordParamRecursion;
type
    TRange = 1..10;
    TPair = record
        a: TRange;
        b: integer;
    end;

var
    i: TPair;

{ Each recursive call passes its own local record 'q' by value - a
  record parameter gets its own per-call frame slots, exactly like a
  scalar parameter, so this must isolate correctly under recursion. }
function Depth(p: TPair; n: integer): integer;
var
    q: TPair;
begin
    if n = 0 then
        Depth := p.a + p.b
    else begin
        q.a := p.a;
        q.b := p.b + n;
        Depth := Depth(q, n - 1);
    end;
end;

{ Mutual recursion through a forward declaration - the completing
  declaration (no parameter list) must still replay the record parameter
  correctly. }
procedure PingPong(p: TPair; n: integer); forward;

procedure PingStep(p: TPair; n: integer);
begin
    writeln('ping n=', n, ' p.a=', p.a, ' p.b=', p.b);
    if n > 0 then PingPong(p, n - 1);
end;

procedure PingPong;
begin
    writeln('pong n=', n, ' p.a=', p.a, ' p.b=', p.b);
    if n > 0 then PingStep(p, n - 1);
end;

begin
    i.a := 3;
    i.b := 0;
    writeln('Depth=', Depth(i, 5));
    PingStep(i, 3);
end.
