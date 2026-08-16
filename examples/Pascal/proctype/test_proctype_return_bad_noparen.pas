program TestProctypeReturnBadNoparen;

// 'h := GetHandler;' (no parens) is always the "take a bare reference"
// reading, never "call GetHandler and use its return value" - even
// though GetHandler takes no arguments. GetHandler's OWN signature (0
// args, returns TProc) doesn't match TProc's own shape (1 int arg,
// returns integer), so this is a clean compile error, not a silent
// fallback to calling it.

type
  TProc = function(x: integer): integer;

var h: TProc;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

function GetHandler: TProc;
begin
  GetHandler := Double;
end;

begin
  h := GetHandler;
end.
