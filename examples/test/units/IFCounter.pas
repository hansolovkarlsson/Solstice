unit IFCounter;

interface

var
  Count: integer;

procedure Bump;

implementation

procedure Bump;
begin
  Count := Count + 1;
end;

initialization
  Count := 100;
  writeln('init IFCounter, Count=', Count);

finalization
  writeln('final IFCounter, Count=', Count);

end.
