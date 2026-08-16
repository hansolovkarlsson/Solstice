program TestExceptBadMessageType;

// 'raise' with a non-string/char message - a clean compile-time type
// error, not a crash.

begin
  raise 5;
end.
