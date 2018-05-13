import "module2"

System.print("main")

class Counter { 
  static create() { 
    var i = 0 
    return Fn.new { i = i + 1 } 
  } 
}

var counter = Counter.create()
System.print(counter.call())
System.print(counter.call())
System.print(counter.call()) 