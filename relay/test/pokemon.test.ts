import { describe, it, expect } from "vitest";
import {
  extractPokemonInfo,
  MAX_POKEMON_ID,
  MIN_POKEMON_ID,
  parsePokemonId,
  pokemonUrl,
  speciesUrl,
} from "../src/pokemon";

describe("parsePokemonId", () => {
  it("整数値をそのまま受理する", () => {
    expect(parsePokemonId(25)).toBe(25);
  });
  it("数値文字列（URL パラメータ想定）も受理する", () => {
    expect(parsePokemonId("1")).toBe(1);
  });
  it("下限・上限の境界を受理する", () => {
    expect(parsePokemonId(MIN_POKEMON_ID)).toBe(MIN_POKEMON_ID);
    expect(parsePokemonId(MAX_POKEMON_ID)).toBe(MAX_POKEMON_ID);
  });
  it("範囲外・非整数・非数値は弾く", () => {
    expect(() => parsePokemonId(0)).toThrow();
    expect(() => parsePokemonId(MAX_POKEMON_ID + 1)).toThrow();
    expect(() => parsePokemonId(1.5)).toThrow();
    expect(() => parsePokemonId("abc")).toThrow();
    expect(() => parsePokemonId(null)).toThrow();
    expect(() => parsePokemonId(undefined)).toThrow();
  });
});

describe("pokemonUrl / speciesUrl", () => {
  it("id をパスに載せる", () => {
    expect(pokemonUrl("https://pokeapi.co/api/v2", 25)).toBe(
      "https://pokeapi.co/api/v2/pokemon/25",
    );
    expect(speciesUrl("https://pokeapi.co/api/v2", 25)).toBe(
      "https://pokeapi.co/api/v2/pokemon-species/25",
    );
  });
  it("ベース URL 末尾スラッシュを吸収する", () => {
    expect(pokemonUrl("https://pokeapi.co/api/v2/", 1)).toBe(
      "https://pokeapi.co/api/v2/pokemon/1",
    );
  });
});

describe("extractPokemonInfo", () => {
  // ピカチュウ(25)を模したミニマルな生レスポンス。
  const pikachuPokemon = {
    name: "pikachu",
    types: [{ slot: 1, type: { name: "electric" } }],
    // 実際は他に大量のフィールドがあるが、抽出対象外なので無視される。
  };
  const pikachuSpecies = {
    names: [
      { language: { name: "en" }, name: "Pikachu" },
      { language: { name: "ja" }, name: "ピカチュウ" },
      { language: { name: "ja-Hrkt" }, name: "ピカチュウ" },
    ],
    genera: [
      { language: { name: "en" }, genus: "Mouse Pokémon" },
      { language: { name: "ja" }, genus: "ねずみポケモン" },
    ],
    flavor_text_entries: [
      {
        language: { name: "ja" },
        flavor_text: "ほっぺたの\n でんきぶくろに\fでんきをためる。",
      },
      { language: { name: "en" }, flavor_text: "It stores electricity." },
    ],
  };

  it("必要フィールドだけを抽出する", () => {
    const info = extractPokemonInfo(25, pikachuPokemon, pikachuSpecies);
    expect(info).toEqual({
      id: 25,
      name_ja: "ピカチュウ",
      name_en: "Pikachu",
      category_ja: "ねずみポケモン",
      types: ["electric"],
      desc_ja: "ほっぺたの でんきぶくろに でんきをためる。",
    });
  });

  it("説明文の改行・改ページを空白へ潰す", () => {
    const info = extractPokemonInfo(25, pikachuPokemon, pikachuSpecies);
    expect(info.desc_ja).not.toMatch(/[\n\f\r]/);
  });

  it("日本語(ja)が無ければ ja-Hrkt へフォールバックする", () => {
    const species = {
      names: [{ language: { name: "ja-Hrkt" }, name: "ぴかちゅう" }],
      genera: [{ language: { name: "ja-Hrkt" }, genus: "ねずみぽけもん" }],
      flavor_text_entries: [
        { language: { name: "ja-Hrkt" }, flavor_text: "でんき。" },
      ],
    };
    const info = extractPokemonInfo(25, pikachuPokemon, species);
    expect(info.name_ja).toBe("ぴかちゅう");
    expect(info.category_ja).toBe("ねずみぽけもん");
    expect(info.desc_ja).toBe("でんき。");
  });

  it("英語名が無ければ pokemon.name にフォールバックする", () => {
    const species = { names: [], genera: [], flavor_text_entries: [] };
    const info = extractPokemonInfo(25, pikachuPokemon, species);
    expect(info.name_en).toBe("pikachu");
  });

  it("欠損フィールドは空文字/空配列へ安全に倒す", () => {
    const info = extractPokemonInfo(999, {}, {});
    expect(info).toEqual({
      id: 999,
      name_ja: "",
      name_en: "",
      category_ja: "",
      types: [],
      desc_ja: "",
    });
  });

  it("複数タイプを slot 順に並べる", () => {
    const bulbasaur = {
      name: "bulbasaur",
      types: [
        { slot: 1, type: { name: "grass" } },
        { slot: 2, type: { name: "poison" } },
      ],
    };
    const info = extractPokemonInfo(1, bulbasaur, {});
    expect(info.types).toEqual(["grass", "poison"]);
  });
});
