program TestDeclorderPtrForwardSplit;

{ PNode forward-references TNode in one 'type' block; TNode is only
  declared in a SECOND 'type' block, with a 'const' interleaved in
  between. Before resolve_pending_pointer_types() was pulled out of
  parse_type_section() and moved to run once after the whole repeated
  section loop, this would have wrongly errored ("targets undeclared
  type 'TNode'") at the end of the FIRST type block. Builds a 3-node
  list exactly like test_ptr_linkedlist.pas. Expected:
  10
  20
  30
  sum=60 }

type
    PNode = ^TNode;

const
    Unused = 1;

type
    TNode = record
        data: integer;
        next: PNode;
    end;

var
    head, cur, tmp: PNode;
    i, sum: integer;

begin
    head := nil;
    for i := 3 downto 1 do begin
        new(tmp);
        tmp^.data := i * 10;
        tmp^.next := head;
        head := tmp;
    end;

    cur := head;
    while cur <> nil do begin
        writeln(cur^.data);
        cur := cur^.next;
    end;

    sum := 0;
    cur := head;
    while cur <> nil do begin
        sum := sum + cur^.data;
        tmp := cur;
        cur := cur^.next;
        dispose(tmp);
    end;
    writeln('sum=', sum);
    writeln('unused const=', Unused);
end.
