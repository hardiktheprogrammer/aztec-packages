// A contract used along with `Parent` contract to test nested calls.
contract Child {
    use dep::aztec::prelude::{AztecAddress, FunctionSelector, PublicMutable, PrivateSet, PrivateContext, Deserialize};

    use dep::aztec::{
        context::{PublicContext, Context, gas::GasOpts},
        protocol_types::{abis::{call_context::CallContext}},
        note::{note_getter_options::NoteGetterOptions, note_header::NoteHeader}
    };
    use dep::value_note::value_note::ValueNote;

    #[aztec(storage)]
    struct Storage {
        current_value: PublicMutable<Field>,
        a_private_value: PrivateSet<ValueNote>,
    }

    // Returns a sum of the input and the chain id and version of the contract in private circuit public input's return_values.
    #[aztec(private)]
    fn value(input: Field) -> Field {
        input + context.chain_id() + context.version()
    }
    // Returns a sum of the input and the chain id and version of the contract in private circuit public input's return_values. 
    // Can only be called from this contract.
    #[aztec(private)]
    #[aztec(internal)]
    fn value_internal(input: Field) -> Field {
        input + context.chain_id() + context.version()
    }

    // Returns base_value + chain_id + version + block_number + timestamp
    #[aztec(public)]
    fn pub_get_value(base_value: Field) -> Field {
        let return_value = base_value
            + context.chain_id()
            + context.version()
            + context.block_number()
            + context.timestamp() as Field;

        return_value
    }

    // Sets `current_value` to `new_value`
    #[aztec(public)]
    fn pub_set_value(new_value: Field) -> Field {
        storage.current_value.write(new_value);
        context.emit_unencrypted_log(new_value);

        new_value
    }

    #[aztec(private)]
    fn private_set_value(new_value: Field, owner: AztecAddress) -> Field {
        let mut note = ValueNote::new(new_value, owner);
        storage.a_private_value.insert(&mut note, true);
        new_value
    }

    #[aztec(private)]
    fn private_get_value(amount: Field, owner: AztecAddress) -> Field {
        let mut options = NoteGetterOptions::new();
        options = options.select(ValueNote::properties().value, amount, Option::none()).select(
            ValueNote::properties().owner,
            owner.to_field(),
            Option::none()
        ).set_limit(1);
        let notes = storage.a_private_value.get_notes(options);
        notes[0].unwrap_unchecked().value
    }

    // Increments `current_value` by `new_value`
    #[aztec(public)]
    fn pub_inc_value(new_value: Field) -> Field {
        let old_value = storage.current_value.read();
        storage.current_value.write(old_value + new_value);
        context.emit_unencrypted_log(new_value);

        new_value
    }

    // Increments `current_value` by `new_value`. Can only be called from this contract.
    #[aztec(public)] 
    #[aztec(internal)]
    fn pub_inc_value_internal(new_value: Field) -> Field {
        let old_value = storage.current_value.read();
        storage.current_value.write(old_value + new_value);
        context.emit_unencrypted_log(new_value);

        new_value
    }

    #[aztec(public)]
    fn set_value_twice_with_nested_first() {
        let _result = Child::at(context.this_address()).pub_set_value(10).call(&mut context);
        storage.current_value.write(20);
        context.emit_unencrypted_log(20);
    }

    #[aztec(public)]
    fn set_value_twice_with_nested_last() {
        storage.current_value.write(20);
        context.emit_unencrypted_log(20);
        let _result = Child::at(context.this_address()).pub_set_value(10).call(&mut context);
    }

    #[aztec(public)]
    // TODO(6052): The logs emitted are currently in the wrong order as we don't update
    // counters for nested public calls
    fn set_value_with_two_nested_calls() {
        Child::at(context.this_address()).set_value_twice_with_nested_first().call(&mut context);
        Child::at(context.this_address()).set_value_twice_with_nested_last().call(&mut context);
        storage.current_value.write(20);
        context.emit_unencrypted_log(20);
    }
}
