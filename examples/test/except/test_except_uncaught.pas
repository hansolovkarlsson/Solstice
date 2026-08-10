program TestExceptUncaught;

// A raise with no enclosing try anywhere - a clean VM Runtime Error
// with the raised message, and a clean non-zero exit, not a crash.

begin
  writeln('before raise');
  raise 'boom';
  writeln('unreachable');
end.
