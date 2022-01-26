Container Design

Implement a class Table which is inserted into the VM
Completely Remove Objects for types? Objects are classes?
- Table, List / Slice, Error are all classes which are inserted into the VM globals
- Indexing needs to be a concept supported by classes
    - what would the definition look like?
    - Should any class be able to provide an index operator?
    - Does this open the door to operator overrides, which are terrible - No, it doesn't have to be implemented
    - Implement a range keyword which can generate an integer iterator with step functionality

    class Table {
        init(structured_input) {

        }
        [index] {

        }
    }