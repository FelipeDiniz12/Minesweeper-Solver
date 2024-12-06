CXX = g++
CXXFLAGS = -Wall -g
RM = rm -rf

TARGET = minesweeper_solver
SRCS = minesweeper.cpp
OBJS = $(SRCS:.cpp=.o)
OPENCV_INSTALL_PATH=

INC_DIR = $(OPENCV_INSTALL_PATH)/include/opencv \
		  $(OPENCV_INSTALL_PATH)/include \
		  /usr/include/X11

LIBS = opencv_core \
	   X11 \
	   GL \
	   GLU

LIB_DIR = $(OPENCV_INSTALL_PATH)/lib \
		  /usr/lib/x86_64-linux-gnu \
		  /usr/lib/X11

LDFLAGS = -g $(addprefix -L, $(LIB_DIR)) \
			 $(addprefix -l, $(LIBS))

CXXFLAGS +=  $(addprefix -I, $(INC_DIR))

all: $(TARGET) run

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(TARGET) *.o