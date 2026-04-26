# 表达式

<!-- grammar_name: expression, search_name: 表达式 -->

<grammar>
expression = name_expression 
           | calc_expression
           | function_call_expression
           | cast_expression
           | struct_init_expression
           | paren_expression
           | borrow_expression
</grammar>

表达式是独立于语句的一类语法。它和语句一样具有副作用，但不同于语句，表达式同时一定具有一个值和类型（当这个值的类型为 `void` 时，值我们认为空），这是它与语句的本质区别。

根据表达式的不同作用，我们可以对其划分为以下几种类型：

1. 名称表达式
2. 运算表达式
3. 函数调用表达式
4. 类型转换表达式
5. 结构体初始化表达式
6. 括号表达式
7. 借用表达式

## 名称表达式

<!-- grammar_name: name_expression, search_name: 标识符 -->

<grammar>
name_expression = identifier_name_expression | field_access_name_expression
</grammar>

名称表达式即根据一个对象的名称获取值的表达式，它的唯一副作用是导致一个对象的移动
（当且仅当指向的名称是变量、且变量类型是非拷贝类型时，触发值移动），表达式的值即名称所对应的值，
也就是表达式的类型也是名称的类型。

名称表达式包括标识符名称表达式和成员访问名称表达式。

### 标识符名称表达式

<!-- grammar_name: identifier_name_expression, search_name: 标识符名称表达式,变量名称表达式 -->

<grammar>
identifier_name_expression = identifier
</grammar>

标识符所对应的名称仅能对应变量名，所以标识符名称表达式也可以称作变量名称表达式。

由于 Lis 语言禁止作用域内标识符隐式覆盖，所以一个标识符所对应的变量名是唯一、无二义性的。不允许引用未定义名称、函数、类型等非变量符号。

标识符名称表达式的值就是标识符对应的符号的值。

### 成员访问名称表达式

<!-- grammar_name: field_access_name_expression, search_name: 成员访问名称表达式 -->

<grammar>
field_access_name_expression = expression . identifier
</grammar>

成员访问表达式用于获取一个结构体实例中某个字段在实例的值。

所以 expression 所对应的类型必须是结构体类型。且必须存在以 identifier 为名称的有效字段。当成员访问名称表达式处于非静态成员函数上下文时，允许 identifier 对应的字段为私有字段。否则，identifier 所对应的字段必须是公共字段。

## 运算表达式

<!-- grammar_name: calc_expression, search_name: 运算表达式 -->

<grammar>
calc_expression = self_calc_expression | binary_calc_expression
</grammar>

运算表达式是一类基本的表达式，它的语义表示讲两个表达式的值进行运算，运算的结果即运算表达式的值。
运算表达式的副作用等同于其所有子表达式的副作用，根据运算表达式子表达式的数量，可以将运算表达式分为自运算表达式和二元表达式。

### 自运算表达式

<!-- grammar_name: self_calc_expression, search_name: 自运算表达式 -->

<grammar>
calc_expression = self_calc_operator expression

self_calc_operator = - | ! | ~
</grammar>

自运算表达式仅包含一个子表达式和一个前置自运算符，用于对单个表达式的值进行单目运算。

自运算表达式的值为运算后的结果，类型与子表达式的类型一致。自运算运算符包含：取反（-）、逻辑非（!）、按位取反（~）。

子表达式必须为可运算的基本类型（数值、布尔等），不允许对结构体、函数等非基础类型使用自运算。

### 二元运算表达式

<!-- grammar_name: binary_calc_expression, search_name: 二元运算表达式 -->

<grammar>
binary_calc_expression = expression binary_calc_operator expression

binary_calc_operator = + | - | * | / | % | == | != | < | > | <= | >= | && | || | & | | | ^ | << | >>
</grammar>

二元运算表达式包含两个子表达式和一个中缀二元运算符，用于对两个值进行算术、逻辑或比较运算。二元运算符可以分为：

- 算术运算符：+、-、*、/、%
- 比较运算符：==、!=、<、>、<=、>=
- 逻辑运算符：&&、||
- 位运算符：&、|、^、<<、>>

左右子表达式类型必须相等，不存在隐式类型转换。

## 函数调用表达式

<!-- grammar_name: function_call_expression, search_name: 函数调用表达式 -->


函数调用表达式用于执行一个函数并获取其返回值。左侧 expression 必须解析为函数类型（普通函数、成员函数），argument_list 为可选的实参列表。

其必须满足的约束为：

1. 实参数量、类型必须与函数形参严格匹配；
2. 表达式的值为函数的返回值，类型为函数返回类型；
3. 若函数返回 void，则表达式值为空，类型为 void；
4. 副作用包含函数执行过程中的所有操作。

