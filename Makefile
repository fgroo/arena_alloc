.PHONY : all re clean fclean test

NAME = arena_alloc.a

CXX = c++

CXXFLAGS = -Wall -Wextra -Werror -std=c++98
DEPFLAGS = -MMD -MP
INC = -I.

LIB_SRC = arena_alloc.cpp
LIB_OBJ = $(LIB_SRC:.cpp=.o)

all : $(NAME)

$(NAME) : $(LIB_OBJ)
	ar rcs $(NAME) $(LIB_OBJ)

%.o : %.cpp arena_alloc.hpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INC) -c $< -o $@

test : test_arena.cpp $(NAME)
	$(CXX) $(CXXFLAGS) $(INC) test_arena.cpp $(NAME) -o test_arena

clean :
	rm -fr $(LIB_OBJ) *.d test_arena.o *.gch

fclean : clean
	rm -fr $(NAME) test_arena main_demo a.out

re : fclean all
