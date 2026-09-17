# UirapuruFX: build de desktop.
#
# Nao precisa de cmake nem de biblioteca externa. Tudo o que o motor usa esta
# dentro de core/, entao um `make` num clone limpo ja compila.

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Icore -Icli

BUILD = build
BIN   = $(BUILD)/uirapuru

SRC = core/pitch_tracker.cpp \
      core/granular_player.cpp \
      core/phasor.cpp \
      core/bird_engine.cpp \
      cli/wav.cpp \
      cli/main.cpp

all: $(BIN)

$(BIN): $(SRC) core/*.h cli/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

# Mesma compilacao com AddressSanitizer e UBSan, pra caçar acesso fora de
# limite e comportamento indefinido. Foi assim que o estouro no granular
# original apareceu.
asan:
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined \
	    -fno-omit-frame-pointer $(SRC) -o $(BUILD)/uirapuru-asan

clean:
	rm -rf $(BUILD)

.PHONY: all asan clean
