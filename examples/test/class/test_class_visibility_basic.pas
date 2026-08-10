program TestClassVisibilityBasic;

// A class with a private field (count) and a private method (Bump),
// both used by public methods. Confirms the class's own code can use
// its own private members freely - self-shorthand access to a private
// field/method from another method of the SAME declaring class is
// unaffected. Three Bump calls: count = 3.

type
  TCounter = class
  private
    count: integer;
    procedure Bump;
  public
    procedure Inc3;
    function Value: integer;
  end;

var c: TCounter;

procedure TCounter.Bump;
begin
  count := count + 1;
end;

procedure TCounter.Inc3;
begin
  Bump;
  Bump;
  Bump;
end;

function TCounter.Value;
begin
  Value := count;
end;

begin
  new(c);
  c.Inc3;
  writeln(c.Value);
  dispose(c);
end.
