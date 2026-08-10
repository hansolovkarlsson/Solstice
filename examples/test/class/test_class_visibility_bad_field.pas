program TestClassVisibilityBadField;

// External code reads a private field directly - a clean compile
// error, not a crash.

type
  TCounter = class
  private
    count: integer;
  public
    procedure Inc3;
  end;

var c: TCounter;

procedure TCounter.Inc3;
begin
  count := count + 1;
end;

begin
  new(c);
  c.Inc3;
  writeln(c.count);
  dispose(c);
end.
