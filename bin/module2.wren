System.print("module2")
System.print("wow %((1..3).map {|n| n * n}.join())")

class Unicorn { 
  construct brown(name) { 
    System.print("My name is " + name + " and I am brown.") 
  } 
}

var dave = Unicorn.brown("Dave")