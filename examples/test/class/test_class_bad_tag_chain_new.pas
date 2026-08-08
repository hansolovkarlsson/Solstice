program TestClassBadTagChainNew;
type
    TCircle = class
        radius: real;
    end;
    TWrapper = record
        c: TCircle;
    end;
    PWrapper = ^TWrapper;
var
    w: PWrapper;
{ Expected: Compile Error - 'new' into a class-typed field reached
  through '^' isn't supported yet (see the tag-write comment in
  parse_new_statement() - a real heap-use-after-free was found and
  fixed here during development, then scoped out with this explicit
  rejection rather than left as a silent "sometimes untagged" gap). }
begin
    new(w);
    new(w^.c);
end.
