(module ;; sort by uses
  (memory 256 256)
  (func $a
    (call $a)
  )
  (func $b
    (call $b)
    (call $b)
  )
  (func $c
    (call $c)
    (call $c)
    (call $c)
  )
)
(module ;; sort by uses take 2
  (memory 256 256)
  (func $a
    (call $a)
  )
  (func $c
    (call $c)
    (call $c)
    (call $c)
  )
  (func $b
    (call $b)
    (call $b)
  )
)
(module ;; some identical uses, so similarity matters
  (memory 256 256)
  (func $a
    (call $a)
  )
  (func $b
    (call $b)
    (nop)
  )
  (func $c
    (call $c)
    (nop)
    (call $c)
  )
)
(module ;; some identical uses, so similarity matters take 2
  (memory 256 256)
  (func $a
    (call $a)
    (nop)
  )
  (func $b
    (call $b)
  )
  (func $c
    (call $c)
    (nop)
    (call $c)
  )
)

