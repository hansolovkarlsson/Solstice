program TestClassNewDispose;
type
    TCircle = class
        radius: real;
        count: integer;
    end;
var
    c: TCircle;
begin
    { Confirms a class variable already resolves as an ordinary pointer
      type (per step 1's design), so new()/dispose() - unmodified,
      pre-existing pointer machinery - already work on it. Field access
      (c.radius) is a later step (3), not exercised here. }
    new(c);
    if c = nil then
        writeln('unexpected: nil after new')
    else
        writeln('allocated ok');
    dispose(c);
    writeln('disposed ok');
end.
