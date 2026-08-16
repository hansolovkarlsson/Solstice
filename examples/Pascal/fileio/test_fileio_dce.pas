program TestFileioDce;
{ f is never read from anywhere - dead-code elimination must still
  perform every assign/rewrite/write/close, since a file variable's
  usage tracking doesn't determine whether its OPERATIONS have real
  side effects (writing to disk). }
var
    f: text;
begin
    assign(f, '/tmp/ouroboros_test_fileio_dce.txt');
    rewrite(f);
    writeln(f, 'hello');
    close(f);
    writeln('done');
end.
