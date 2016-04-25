/*
 * Copyright 2016 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <iomanip>
#include <pass.h>
#include <wasm.h>

namespace wasm {

using namespace std;

// Prints metrics on a module. If run more than once, shows the diff.

struct Metrics : public WalkerPass<PostWalker<Metrics, UnifiedExpressionVisitor<Metrics>>> {
  static Metrics *lastMetricsPass;

  map<const char *, int> counts;

  virtual void count(Expression* curr) {
    auto name = getExpressionName(curr);
    counts[name]++;
  }

  void visitExpression(Expression* curr) {
    count(curr);
  }

  void finalize(PassRunner *runner, Module *module) override {
    ostream &o = cout;
    o << "Counts"
      << "\n";
    vector<const char*> keys;
    int total = 0;
    for (auto i : counts) {
      keys.push_back(i.first);
      total += i.second;
    }
    sort(keys.begin(), keys.end(), [](const char* a, const char* b) -> bool {
      return strcmp(b, a) > 0;
    });
    for (auto* key : keys) {
      auto value = counts[key];
      o << " " << left << setw(25) << key << ": " << setw(8)
        << value;
      if (lastMetricsPass) {
        if (lastMetricsPass->counts.count(key)) {
          int before = lastMetricsPass->counts[key];
          int after = value;
          if (after - before) {
            if (after > before) {
              Colors::red(o);
            } else {
              Colors::green(o);
            }
            o << right << setw(8);
            o << showpos << after - before << noshowpos;
            Colors::normal(o);
          }
        }
      }
      o << "\n";
    }
    o << left << setw(26) << "Total" << ": " << setw(8) << total << '\n';
    lastMetricsPass = this;
  }
};

// Detailed metrics, drilling down into specific opcodes for some node types.

struct DetailedMetrics : public Metrics {
  void count(Expression* curr) override {
    const char *name;
    if (auto* unary = curr->dynCast<Unary>()) {
      switch (unary->op) {
        case Clz:              name = "unary-clz";     break;
        case Ctz:              name = "unary-ctz";     break;
        case Popcnt:           name = "unary-popcnt";  break;
        case EqZ:              name = "unary-eqz";     break;
        case Neg:              name = "unary-neg";     break;
        case Abs:              name = "unary-abs";     break;
        case Ceil:             name = "unary-ceil";    break;
        case Floor:            name = "unary-floor";   break;
        case Trunc:            name = "unary-trunc";   break;
        case Nearest:          name = "unary-nearest"; break;
        case Sqrt:             name = "unary-sqrt";    break;
        case ExtendSInt32:     name = "unary-extend_s/i32"; break;
        case ExtendUInt32:     name = "unary-extend_u/i32"; break;
        case WrapInt64:        name = "unary-wrap/i64"; break;
        case TruncSFloat32:    name = "unary-trunc_s/f32"; break;
        case TruncUFloat32:    name = "unary-trunc_u/f32"; break;
        case TruncSFloat64:    name = "unary-trunc_s/f64"; break;
        case TruncUFloat64:    name = "unary-trunc_u/f64"; break;
        case ReinterpretFloat: name = "unary-reinterpret/f*"; break;
        case ConvertUInt32:    name = "unary-convert_u/i32"; break;
        case ConvertSInt32:    name = "unary-convert_s/i32"; break;
        case ConvertUInt64:    name = "unary-convert_u/i64"; break;
        case ConvertSInt64:    name = "unary-convert_s/i64"; break;
        case PromoteFloat32:   name = "unary-promote/f32"; break;
        case DemoteFloat64:    name = "unary-demote/f64"; break;
        case ReinterpretInt:   name = "unary-reinterpret/i*"; break;
        default: abort();
      }
    } else {
      name = getExpressionName(curr);
    }
    counts[name]++;
  }
};

Metrics *Metrics::lastMetricsPass;

static RegisterPass<Metrics> registerPass("metrics", "reports metrics");

static RegisterPass<DetailedMetrics> registerDetailedPass("detailed-metrics", "reports detailed metrics");

} // namespace wasm
