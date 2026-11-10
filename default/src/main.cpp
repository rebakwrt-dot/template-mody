{
  "geode": "5.11.2",
  "gd": { "win": "2.2081", "android": "2.2081", "mac": "2.2081", "ios": "2.2081" },
  "id": "yourname.rl-bot",
  "name": "RL Bot",
  "version": "v1.0.0",
  "developer": "yourname",
  "description": "A reinforcement learning AI that learns to beat the current level. Knowledge is saved per level.",
  "settings": {
    "enabled": {
      "type": "bool", "name": "Enable AI", "default": false,
      "description": "Let the AI play the level."
    },
    "learning-rate": {
      "type": "float", "name": "Learning rate", "default": 0.2, "min": 0.01, "max": 1.0,
      "description": "How fast Q-values change (alpha)."
    },
    "discount": {
      "type": "float", "name": "Discount factor", "default": 0.98, "min": 0.5, "max": 0.999,
      "description": "How much future reward matters (gamma)."
    },
    "reward-per-percent": {
      "type": "float", "name": "Reward per percent", "default": 1.0, "min": 0.0, "max": 10.0,
      "description": "Reward gained for each percent of progress."
    },
    "exploration-bonus": {
      "type": "float", "name": "Exploration bonus", "default": 2.0, "min": 0.0, "max": 20.0,
      "description": "Extra reward per percent beyond the best percent reached so far."
    },
    "death-penalty": {
      "type": "float", "name": "Death penalty", "default": 5.0, "min": 0.0, "max": 50.0,
      "description": "Subtracted from the last steps before dying (reward there is also set to 0)."
    },
    "death-window": {
      "type": "int", "name": "Death window (steps)", "default": 15, "min": 1, "max": 200,
      "description": "How many steps before death get the penalty."
    },
    "epsilon-start": {
      "type": "float", "name": "Exploration rate (start)", "default": 0.3, "min": 0.0, "max": 1.0,
      "description": "Chance of a random action at attempt 0."
    },
    "epsilon-min": {
      "type": "float", "name": "Exploration rate (min)", "default": 0.02, "min": 0.0, "max": 1.0,
      "description": "Exploration never drops below this."
    },
    "epsilon-decay": {
      "type": "float", "name": "Exploration decay", "default": 0.995, "min": 0.9, "max": 1.0,
      "description": "Multiplier applied to exploration rate every attempt."
    },
    "jump-chance": {
      "type": "float", "name": "Random jump chance", "default": 0.15, "min": 0.0, "max": 1.0,
      "description": "When exploring, probability of choosing 'hold'."
    },
    "action-repeat": {
      "type": "int", "name": "Action repeat (frames)", "default": 2, "min": 1, "max": 10,
      "description": "The AI decides once every N frames."
    },
    "reset-knowledge": {
      "type": "bool", "name": "Reset knowledge of current level", "default": false,
      "description": "Enable, then restart the level: the saved data for that level is wiped (switch turns itself off)."
    }
  }
}
