#include <string>
#include <vector>
#include <cctype>
#include <iostream>

std::string parseFunction(const std::string& signature)
{
    // grab just the method name - it sits between the last space and the '('
    std::size_t start = signature.find_last_of(' ');
    std::size_t end   = signature.find('(');
    std::string name  = signature.substr(start + 1, end - start - 1);

    // drop the "test" prefix
    const std::string prefix = "test";
    if (name.rfind(prefix, 0) == 0)
        name = name.substr(prefix.size());

    // split into words at each capital letter
    std::vector<std::string> words;
    std::string currentWord;
    for (char c : name)
    {
        if (std::isupper(c) && !currentWord.empty())
        {
            words.push_back(currentWord);
            currentWord.clear();
        }
        currentWord += c;
    }
    if (!currentWord.empty())
        words.push_back(currentWord);

    // first word keeps its capital, the rest go lowercase, joined by single spaces
    std::string result;
    for (int i = 0; i < words.size(); ++i)
    {
        std::string word = words[i];
        if (i > 0)
        {
            for (char& ch : word)
                ch = std::tolower(ch);
            result += ' ';
        }
        result += word;
    }
    return result;
}

int main()
{
    std::cout << parseFunction("public void testCowsCanBeMilked()") << "\n";
    std::cout << parseFunction("public void testSheepAreNotTheOnlyFruit()") << "\n";
    // Cows can be milked
    // Sheep are not the only fruit
}
