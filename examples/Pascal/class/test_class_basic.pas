program TestClassBasic;
type
    TCircle = class
        radius: real;
        count: integer;
        procedure SetRadius(r: real);
        procedure Grow(var by: integer);
        function Area: real;
    end;
var
    c: TCircle;
begin
    { Step 1 checkpoint: declaration parsing only - no .field/.Method
      access yet (steps 3/5), so there's nothing to do with c beyond
      declaring it. Confirms the class type declaration (fields +
      method headers) parses cleanly and produces valid, if trivial,
      bytecode. }
    writeln('declared ok');
end.
