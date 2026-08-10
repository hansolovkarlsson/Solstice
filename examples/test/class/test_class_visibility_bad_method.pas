program TestClassVisibilityBadMethod;

// External code calls a private method directly - a clean compile
// error, not a crash.

type
  TCounter = class
  private
    procedure Bump;
  end;

var c: TCounter;

procedure TCounter.Bump;
begin
end;

begin
  new(c);
  c.Bump;
  dispose(c);
end.
