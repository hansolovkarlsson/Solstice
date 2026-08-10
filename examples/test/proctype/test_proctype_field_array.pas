program TestProctypeFieldArray;

// An array-typed class field whose element type is a named procedural
// type. Needed its own fix beyond the plain scalar field case - an
// array field access is always a TERMINAL step (see
// docs/LANGUAGE.md#classes), built and returned directly rather than
// falling through to the shared "field followed by '(' means call it"
// check, so that check had to be duplicated at both array-field-
// terminal sites (parse_heap_deref_read() and, separately,
// parse_self_shorthand_read() - the same duplication that caused a
// real bug during the array-typed-fields feature itself).
// Double(5) = 10, Triple(5) = 15.

type
  TProc = function(x: integer): integer;
  TFoo = class
    handlers: array[0..1] of TProc;
  end;

var f: TFoo;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

function Triple(x: integer): integer;
begin
  Triple := x * 3;
end;

begin
  new(f);
  f.handlers[0] := Double;
  f.handlers[1] := Triple;
  writeln(f.handlers[0](5));
  writeln(f.handlers[1](5));
  dispose(f);
end.
