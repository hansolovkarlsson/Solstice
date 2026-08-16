program TestClassTagBasic;
type
    TCircle = class
        radius: real;
    end;
var
    c: TCircle;
begin
    { Every class instance now reserves heap offset 0 for a hidden
      runtime type tag (write-only for now - nothing reads it back
      until virtual dispatch consumes it later). This test just
      confirms ordinary field read/write still works correctly with
      every field's real heap offset shifted by 1 to make room for it -
      see the disassembly-verified bytecode in this feature's own
      commit message/roadmap entry for direct confirmation of the tag
      write itself. }
    new(c);
    c.radius := 2.0;
    writeln(c.radius);
    dispose(c);
end.
