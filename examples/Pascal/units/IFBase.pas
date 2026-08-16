unit IFBase;

interface

var
  BaseVal: integer;

implementation

initialization
  BaseVal := 1;
  writeln('init IFBase');
finalization
  writeln('final IFBase');
end.
